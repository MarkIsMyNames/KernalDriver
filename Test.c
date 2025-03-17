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

// Update these paths/names if needed.
#define DEVICE_FILE      "/dev/Marks_Driver"
#define PROC_FILE        "/proc/MDriver/Marks_Driver"

// IOCTL definitions - make sure they match the driver definitions.
#define IOCTL_GET _IOR('L', 1, short)
#define IOCTL_SET _IOW('L', 2, short)

#define BUF_SIZE 512

// Thread function prototypes
void *blocking_read_thread(void *arg);
void *proc_read_thread(void *arg);
void *ioctl_thread(void *arg);

// Function to display menu
void print_menu() {
    printf("\n=========================\n");
    printf(" Marks_Driver User App\n");
    printf("=========================\n");
    printf("Select an option:\n");
    printf("1. Start a blocking read from %s\n", DEVICE_FILE);
    printf("2. Read from proc file: %s\n", PROC_FILE);
    printf("3. IOCTL GET: Get button status\n");
    printf("4. IOCTL SET: Set button status\n");
    printf("5. Exit\n");
    printf("Enter your choice: ");
}

int main(void)
{
    int choice;
    int ret;
    pthread_t tid;

    while (1) {
        print_menu();
        if(scanf("%d", &choice) != 1) {
            fprintf(stderr, "Invalid input.\n");
            // Clear stdin buffer
            while(getchar() != '\n');
            continue;
        }

        switch (choice) {
            case 1:
                ret = pthread_create(&tid, NULL, blocking_read_thread, NULL);
                if (ret != 0) {
                    fprintf(stderr, "Error creating blocking read thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid); // detach for demonstration purposes
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
                ret = pthread_create(&tid, NULL, ioctl_thread, (void *) (intptr_t)1); // 1 indicates IOCTL_GET
                if (ret != 0) {
                    fprintf(stderr, "Error creating IOCTL GET thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid);
                }
                break;
            case 4: {
                ret = pthread_create(&tid, NULL, ioctl_thread, (void *) (intptr_t)2); // 2 indicates IOCTL_SET
                if (ret != 0) {
                    fprintf(stderr, "Error creating IOCTL SET thread: %s\n", strerror(ret));
                } else {
                    pthread_detach(tid);
                }
                break;
            }
            case 5:
                printf("Exiting...\n");
                exit(0);
            default:
                printf("Invalid option. Please try again.\n");
        }
        // Small delay to keep interactive feel.
        sleep(1);
    }

    return 0;
}

/*
 * Thread function for a blocking read from the device file.
 * It opens the device and calls read() in a blocking manner.
 */
void *blocking_read_thread(void *arg)
{
    (void)arg;  // Unused parameter
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
        close(fd);
        pthread_exit(NULL);
    }

    buf[bytes_read] = '\0';
    printf("[Blocking Read Thread] Read %zd bytes from device:\n%s\n", bytes_read, buf);

    close(fd);
    pthread_exit(NULL);
}

/*
 * Thread function to read from the proc file.
 */
void *proc_read_thread(void *arg)
{
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

/*
 * Thread function to perform IOCTL calls.
 *
 * When 'arg' is 1 perform IOCTL_GET.
 * When 'arg' is 2 perform IOCTL_SET.
 */
void *ioctl_thread(void *arg)
{
    int option = (int)(intptr_t)arg;
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
        // IOCTL_GET
        ret = ioctl(fd, IOCTL_GET, &button_status);
        if (ret < 0) {
            perror("[IOCTL Thread] IOCTL_GET failed");
        } else {
            printf("[IOCTL Thread] IOCTL_GET: Current button status: %d\n", button_status);
        }
    } else if (option == 2) {
        // IOCTL_SET
        printf("[IOCTL Thread] Enter new button status (number): ");
        if(scanf("%hd", &new_status) != 1){
            fprintf(stderr, "[IOCTL Thread] Invalid input for button status\n");
            close(fd);
            pthread_exit(NULL);
        }
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
