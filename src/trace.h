/* Lighthouse Pinball: the log lines (trace.c) */
#ifndef TRACE_H
#define TRACE_H
extern char trace_buf[40];
u8 trace_event(u16 tick, u8 i);
void trace_line(const char *what, u16 tick, u32 v, u8 digits);
#endif
