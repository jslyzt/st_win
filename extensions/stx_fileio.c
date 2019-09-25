#include <stdlib.h>
#include "stx_fileio.h"

#define STX_FILEIO_SIGNUM SIGUSR2

typedef struct {
    st_netfd_t data_fd;
    st_netfd_t control_fd;
    pid_t pid;
} fileio_data_t;

#define FILEREADER_MAX_READ 1024

typedef struct {
    off_t offset;
    ssize_t nbytes;
} file_reader_cb_t;

/*
 * Fork a process to read a file and return its pid.  Receives
 * offset/length commands from control stream and sends corresponding data
 * to out stream.  A zero length on the control stream signals an end.
 *
 * @param fd stream from which to read
 * @param control_out receives the file descriptor to which control commands can be sent
 * @param fd_out receives the file descriptor from which the output of the command can be read.
 * @return PID of the process created to execute the command
 */
pid_t
file_reader(int fd, int* fd_control, int* fd_out) {
    pid_t pid;
    int control_pipe[2], out_pipe[2];

    if (pipe(control_pipe) < 0 || pipe(out_pipe) < 0) {
        return (pid_t) - 1;
    }
    pid = fork();
    if (pid == (pid_t) - 1) {
        close(control_pipe[0]);
        close(control_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return pid;
    } else if (pid == (pid_t) 0) {
        // child
        off_t pos = 0;
        file_reader_cb_t cb;
        char buf[FILEREADER_MAX_READ];
        if (fd == -1)
            _exit(EXIT_FAILURE);

        while (sizeof(cb) == read(control_pipe[0], &cb, sizeof(cb))) {
            ssize_t nb;
            if (0 >= cb.nbytes) {
                goto clean_exit;
            }
            if (pos != cb.offset) {
                pos = lseek(fd, cb.offset, SEEK_SET);
                if (pos == (off_t)-1) {
                    break;
                }
            }
            nb = read(fd, buf, cb.nbytes);
            if (nb == (ssize_t)-1) {
                break;
            }
            pos += nb;
            write(out_pipe[1], (char*)&nb, sizeof(nb));
            write(out_pipe[1], buf, nb);
        }
        perror("ERROR: file_reader: ");
clean_exit:
        close(control_pipe[0]);
        close(control_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        _exit(EXIT_SUCCESS);
    }

    // parent
    close(out_pipe[1]);
    close(control_pipe[0]);
    *fd_out = out_pipe[0];
    *fd_control = control_pipe[1];
    return pid;
}

// fileio_data_t destructor callback
static void
fileio_data_destructor(void* dat_in) {
    if (dat_in) {
        fileio_data_t* dat = (fileio_data_t*)dat_in;
        file_reader_cb_t cb;
        cb.offset = 0;
        cb.nbytes = 0;
        st_write(dat->control_fd, (char*)&cb, sizeof(cb), ST_UTIME_NO_TIMEOUT);
        waitpid(dat->pid, NULL, 0);
        st_netfd_close(dat->control_fd);
        st_netfd_close(dat->data_fd);
        free(dat_in);
    }
}

// Retrieve fileio_data_t struct from an st descriptor.  Create and store a new one if needed.
static fileio_data_t* get_fileio_data(st_netfd_t fd) {
    fileio_data_t* dat = (fileio_data_t*)st_netfd_getspecific(fd);
    if (!dat) {
        int fd_control, fd_out;
        pid_t pid = file_reader(st_netfd_fileno(fd), &fd_control, &fd_out);
        if (pid != (pid_t) - 1) {
            dat = (fileio_data_t*)calloc(1, sizeof(fileio_data_t));
            dat->control_fd = st_netfd_open(fd_control);
            dat->data_fd = st_netfd_open(fd_out);
            dat->pid = pid;
            st_netfd_setspecific(fd, dat, fileio_data_destructor);
        }
    }
    return dat;
}

/*
 * Read data from the specified section of a file.  Uses a forked
 * file_reader process to do the actual reading so as to avoid causing all
 * State Threads to block.
 *
 * @param fd must refer to a seekable file.
 * @param offset absolute offset within the file
 * @param buf output buffer
 * @param nbytes size of the output buffer
 * @param timeout
 */
ssize_t stx_file_read(st_netfd_t fd, off_t offset, void* buf, size_t nbytes, st_utime_t timeout) {
    fileio_data_t* dat = get_fileio_data(fd);
    if (dat) {
        file_reader_cb_t cb;
        ssize_t ret = (ssize_t) - 1;
        cb.offset = offset;
        cb.nbytes = nbytes;
        st_write(dat->control_fd, (char*)&cb, sizeof(cb), timeout);
        if (sizeof(ret) == st_read(dat->data_fd, (char*)&ret, sizeof(ret), timeout) && 0 < ret && ret <= nbytes) {
            return st_read(dat->data_fd, buf, ret, timeout);
        } else {
            return ret;
        }
    }
    return (ssize_t) - 1;
}
