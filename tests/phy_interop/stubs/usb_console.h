void usb_console_write(const char *message);

#include <stdbool.h>
bool usb_console_try_write(const char *line);
