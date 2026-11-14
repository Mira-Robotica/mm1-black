#pragma once
#include <stddef.h>
#include <stdint.h>
namespace lab {
void stream_tick();
bool stream_busy(); // Includes failed restoration; STATUS remains available.
const char *stream_state();
uint32_t stream_session_id();
void stream_disconnect(uint32_t connection);
void stream_command(const char *line, uint32_t connection, char *out, size_t size);
// Called only when the network can enqueue a whole line. No growing sample queue.
bool stream_output(char *out, size_t size, bool replace_snapshot = false);
extern const char STREAM_HEADER[];
}
