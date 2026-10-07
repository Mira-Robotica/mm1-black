#pragma once
// Narrow hooks used only by the vendored Arduino wrapper in MM1_LAB.
void p4_sparkfun_stage(const char *stage, int rc);
void p4_sparkfun_io_error();
void p4_sparkfun_empty_read();
void p4_sparkfun_reset();
void p4_sparkfun_probe_result(unsigned char address, unsigned char rc);
