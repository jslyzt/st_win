#ifndef __STX_FILEIO_H__
#define __STX_FILEIO_H__

#include <st.h>

#ifdef __cplusplus
extern "C" {
#endif

extern ssize_t stx_file_read(st_netfd_t fd, off_t offset, void* buf, size_t nbytes, st_utime_t timeout);

#ifdef __cplusplus
}
#endif
#endif /* !__STX_FILEIO_H__ */
