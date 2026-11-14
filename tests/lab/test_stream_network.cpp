// Compile the actual command dispatcher/TCP queue against a nonblocking socket fake.
#include "lab/network.cpp"
#include <cassert>
#include <iostream>
#include <string>
bool streaming=false,restoring=false;
unsigned disconnects=0,pings=0,stops=0,captures=0,cal_commands=0,resets_requested=0;
unsigned next_sample=0,generated=0,replacements=0;
namespace lab {
void calibration_disconnect(CalOwner) {}
bool calibration_busy() { return false; }
bool calibration_blocks_capture() { return false; }
const char *calibration_state() { return "IDLE"; }
void calibration_command(const char*,CalOwner,char *out,size_t n) { ++cal_commands;snprintf(out,n,"# OK CAL_IMU\n"); }
bool capture_busy() { return false; }
bool capture_ready() { return false; }
void capture_cancel() {}
bool capture_start(uint32_t,bool) { ++captures;return true; }
const char *capture_state() { return "IDLE"; }
const char *laser_state() { return "IDLE"; }
const Sample &capture_sample() { static Sample s;return s; }
bool stream_busy() { return streaming || restoring; }
const char *stream_state() { return restoring ? "RESTORING" : streaming ? "STREAMING" : "IDLE"; }
uint32_t stream_session_id() { return 1; }
void stream_disconnect(uint32_t) { if(streaming) { streaming=false;restoring=true;++disconnects; } }
void stream_command(const char *line,uint32_t,char *out,size_t n) {
    out[0]=0;
    if(strstr(line,"PING")) {++pings;snprintf(out,n,"# OK STREAM_IMU PING session_id=1\n");}
    if(strstr(line,"STOP")) {++stops;streaming=false;restoring=true;}
}
bool stream_output(char *out,size_t n,bool replace) {
    if(!streaming || fake_ms<next_sample) return false;
    if(replace) ++replacements;
    next_sample=fake_ms+500;
    snprintf(out,n,"1,%u,values\n",++generated);return true;
}
}
P4ImuDiagnostics p4_imu_diagnostics() { return {}; }
P4ImuLabInfo p4_imu_lab_info() { return {}; }
void p4_imu_request_reset() { ++resets_requested; }
void receive(const std::string &line) { net_rx.insert(net_rx.end(),line.begin(),line.end()); }
void reset_connection() {
    streaming=false;restoring=false;close_client();
    client=WiFiClient(true);server_started=true;client_activity=fake_ms;
    sent_bytes.clear();net_rx.clear();send_error=0;send_limit=2048;
    next_sample=fake_ms;generated=0;streaming=true;
}
void drain() { for(int i=0;i<100 && (tx_sent<tx_size || control[0]);++i) service_tcp(); }
int main() {
    reset_connection();
    // Busy guards apply equally to serial and TCP; STATUS stays allowed.
    for(bool serial:{false,true}) {
        for(const char *line:{"CAPTURE 1 1","CAPTURE_IMU 1 1","CAL_IMU START","IMU_RESET"}) {
            command(line,serial);drain();
        }
    }
    assert(!captures && !cal_commands && !resets_requested);
    command("STATUS",false);drain();assert(sent_bytes.find("# OK STATUS")!=std::string::npos);
    assert(sent_bytes.find("reset_reason=4")!=std::string::npos);
    reset_connection();send_limit=2;
    service_tcp();assert(tx_snapshot && !tx_sent);
    service_tcp();assert(tx_sent==2);
    receive("STREAM_IMU STOP session_id=1\n");service_tcp();
    assert(stops==1 && restoring && tx_sent==4);drain();
    assert(sent_bytes=="1,1,values\n"); // STOP was consumed before the partial snapshot finished.
    reset_connection();send_limit=2;service_tcp();service_tcp();
    receive("STREAM_IMU PING session_id=1\n");service_tcp();assert(pings==1 && control[0]);drain();
    assert(sent_bytes=="1,1,values\n# OK STREAM_IMU PING session_id=1\n");
    reset_connection();send_error=EAGAIN;service_tcp();service_tcp();
    fake_ms+=500;service_tcp();assert(replacements==1 && !tx_sent && std::string(tx).find("1,2,")==0);
    fake_ms+=500;service_tcp();assert(replacements==2 && generated==3);
    send_error=0;service_tcp();assert(sent_bytes=="1,3,values\n");
    reset_connection();send_error=EAGAIN;service_tcp();fake_ms+=2001;service_tcp();
    assert(!client.connected() && restoring && disconnects==1);
    reset_connection();send_error=EPIPE;service_tcp();service_tcp();assert(restoring && disconnects==2);
    reset_connection();radio_ready=true;state=State::Connected;link_lost=true;WiFi.online=0;
    lab::network_tick();assert(restoring && disconnects==3); // Wi-Fi loss invokes the same cooperative restore.
    reset_connection();client.stop();service_tcp();assert(restoring && disconnects==4);
    // Existing commands work again after restoration; capture output is never classified as a stream snapshot.
    reset_connection();streaming=false;
    command("CAPTURE_IMU 1 1",false);drain();assert(captures==1);
    queue_reply("request_id,sample_index\n1,1\n");assert(!tx_snapshot);drain();
    assert(sent_bytes.find("request_id,sample_index\n1,1\n")!=std::string::npos);
    command("CAL_IMU STATUS",false);drain();assert(cal_commands==1);
    command("IMU_RESET",false);drain();assert(resets_requested==1);
    // A full-size control line survives reuse of the formatting workspace by
    // serial commands and another queued reply while TCP sends only fragments.
    reset_connection();streaming=false;send_limit=17;
    const std::string large = "# " + std::string(2000, 'x') + "\n";
    strcpy(workspace,large.c_str());queue_reply(workspace);
    service_tcp();assert(tx_sent==17);
    command("CAL_IMU STATUS",true); // Serial producer reuses the workspace.
    command("CAL_IMU STATUS",false); // Pending control copies its own bytes.
    memset(workspace,'z',sizeof(workspace));
    for(int i=0;i<150 && (tx_sent<tx_size || control[0]);++i) service_tcp();
    assert(sent_bytes==large+"# OK CAL_IMU\n");
    std::cout<<"TCP/serial exclusion, partial writes, control priority, backpressure and disconnect tests passed\n";
}
