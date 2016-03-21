#include <stdbool.h>

#define VOLUME_SET_PRIVILEGE "http://tizen.org/privilege/volume.set"
#define RECORDER_PRIVILEGE "http://tizen.org/privilege/recorder"

void cynara_log(const char *string, int cynara_status);
bool cynara_check_privilege(int fd, const char *privilege);
