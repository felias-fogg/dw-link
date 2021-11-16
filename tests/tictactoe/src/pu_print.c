// picoUART print functions

extern void putx(char);

void prints(const char* s)
{
    while (*s) putx(*s++);
}

void prints_P(const __flash char* s)
{
    while (*s) putx(*s++);
}
