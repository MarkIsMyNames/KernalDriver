/*
 * marks_app.c - A multithreaded user-space application to interact with Marks_Driver.
 *
 * Compile with: gcc -o marks_app marks_app.c -lpthread
 *
 * Note: Run this application as a user with appropriate privileges
 * to access /dev/Marks_Driver and /proc/MDriver/Marks_Driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#define DEVICE_FILE      "/dev/Marks_Driver"
#define PROC_FILE        "/proc/MDriver/Marks_Driver"

#define IOCTL_GET _IOR('L', 1, short)
#define IOCTL_SET _IOW('L', 2, short)

#define BUF_SIZE 512

pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER; // Mutex to synchronize input from multiple threads

//function signatures for thread functions
void *blocking_read_thread(void *arg);
void *proc_read_thread(void *arg);
void *ioctl_thread(void *arg);

void print_menu() {
    printf("\n========================="
           "\n Marks_Driver User App"
           "\n========================="
           "\nSelect an option:"
           "\n1. Start a blocking read from %s"
           "\n2. Read from proc file: %s"
           "\n3. IOCTL GET: Get button status"
           "\n4. IOCTL SET: Set button status"
           "\n5. Exit"
           "\nEnter your choice: ", DEVICE_FILE, PROC_FILE);
}

int main(void) {
    int choice;
    int ret;
    pthread_t tid;

    while (1) {
        print_menu();
        pthread_mutex_lock(&input_lock); //locks input to avoid race conditions
        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Invalid input.\n");
            while (getchar() != '\n'); //clear input buffer
            pthread_mutex_unlock(&input_lock); //unlocks mutex if input is valid
            continue;
        }
        pthread_mutex_unlock(&input_lock); //unlocks after right input

        switch (choice) {
            case 1:
                ret = pthread_create(&tid, NULL, blocking_read_thread, NULL);
                if (ret != 0) {
                    fprintf(stderr, "Error creating blocking read thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid); //detaches to avoid a memory leak
                }
                break;
            case 2:
                ret = pthread_create(&tid, NULL, proc_read_thread, NULL);
                if (ret != 0) {
                    fprintf(stderr, "Error creating proc read thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid);
                }
                break;
            case 3:
                ret = pthread_create(&tid, NULL, ioctl_thread, (void *)(intptr_t)1);
                if (ret != 0) {
                    fprintf(stderr, "Error creating IOCTL GET thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid);
                }
                break;
            case 4:
                ret = pthread_create(&tid, NULL, ioctl_thread, (void *)(intptr_t)2);
                if (ret != 0) {
                    fprintf(stderr, "Error creating IOCTL SET thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid);
                }
                break;
            case 5:
                printf("Exiting...\n");
                pthread_mutex_destroy(&input_lock); //destroy the mutex to clean up resources
                exit(0);
            default:
                printf("Invalid option. Please try again.\n");
        }
        sleep(1); //readability delay
    }

    return 0;
}

// thread function for blocking reading from device file
void *blocking_read_thread(void *arg) {
    (void)arg; //just used to acknowledge that arg is not used
    int fd;
    char buf[BUF_SIZE];
    ssize_t bytes_read;

    printf("[Blocking Read Thread] Opening the device file: %s\n", DEVICE_FILE);
    fd = open(DEVICE_FILE, O_RDONLY);
    if (fd < 0) {
        perror("[Blocking Read Thread] Error opening device file");
        pthread_exit(NULL);
    }

    printf("[Blocking Read Thread] Waiting for data (blocking read)...\n");
    bytes_read = read(fd, buf, sizeof(buf)-1);
    if (bytes_read < 0) {
        perror("[Blocking Read Thread] Error reading from device");
        close(fd); //good practice to free up system resources again
        pthread_exit(NULL);
    }

    buf[bytes_read] = '\0'; // null-terminating the string
    printf("[Blocking Read Thread] Read %zd bytes from device:\n%s\n", bytes_read, buf);

    close(fd);
    pthread_exit(NULL);
}

void *proc_read_thread(void *arg) {
    (void)arg;
    int fd;
    char buf[BUF_SIZE];
    ssize_t bytes_read;

    printf("[Proc Read Thread] Opening the proc file: %s\n", PROC_FILE);
    fd = open(PROC_FILE, O_RDONLY);
    if (fd < 0) {
        perror("[Proc Read Thread] Error opening proc file");
        pthread_exit(NULL);
    }

    bytes_read = read(fd, buf, sizeof(buf)-1);
    if (bytes_read < 0) {
        perror("[Proc Read Thread] Error reading from proc file");
        close(fd);
        pthread_exit(NULL);
    }

    buf[bytes_read] = '\0';
    printf("[Proc Read Thread] Content read from proc file:\n%s\n", buf);

    close(fd);
    pthread_exit(NULL);
}

void *ioctl_thread(void *arg) {
    int option = (int)(intptr_t)arg; //type casting the arg to intptr_t
    int fd;
    int ret;
    short button_status;
    short new_status;

    printf("[IOCTL Thread] Opening device file for IOCTL operations: %s\n", DEVICE_FILE);
    fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        perror("[IOCTL Thread] Error opening device file");
        pthread_exit(NULL);
    }

    if (option == 1) {
        ret = ioctl(fd, IOCTL_GET, &button_status);
        if (ret < 0) {
            perror("[IOCTL Thread] IOCTL_GET failed");
        } else {
            printf("[IOCTL Thread] IOCTL_GET: Current button status: %d\n", button_status);
        }
    } else if (option == 2) {
        printf("[IOCTL Thread] Enter new button status (number): ");
        pthread_mutex_lock(&input_lock);
        if (scanf("%hd", &new_status) != 1) {
            fprintf(stderr, "[IOCTL Thread] Invalid input for button status\n");
            while (getchar() != '\n');
            pthread_mutex_unlock(&input_lock);
            close(fd);
            pthread_exit(NULL);
        }
        while (getchar() != '\n');
        pthread_mutex_unlock(&input_lock);

        ret = ioctl(fd, IOCTL_SET, &new_status);
        if (ret < 0) {
            perror("[IOCTL Thread] IOCTL_SET failed");
        } else {
            printf("[IOCTL Thread] IOCTL_SET: Changed button status to: %d\n", new_status);
        }
    } else {
        printf("[IOCTL Thread] Unknown IOCTL option provided.\n");
    }

    close(fd);
    pthread_exit(NULL);
}