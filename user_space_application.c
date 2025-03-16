#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>

#define DEVICE_PATH "/dev/Marks_Driver"
#define PROC_PATH "/proc/marks_proc"
#define BUFFER_SIZE 256
#define NUM_THREADS 3
#define IOCTL_MAGIC 'C'
#define IOCTL_COMMAND _IOW(IOCTL_MAGIC, 1, char[32])

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
char colour[32]; // Use a string instead of a single char

volatile int running = 1; // Variable to ensure that infinite loops are avoided

// IOCTL function for changing the colour of the portal
void *perform_ioctl(void *arg) {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device FOR IOCTL");
        return(NULL);
    }
    while (running) { // Check termination variable
        pthread_mutex_lock(&lock); // Lock for user input
        printf("\nPlease enter a colour for the portal (Format: 'red', 'blue', etc.) Or type 'Exit': ");
        scanf("%31s", colour);
        pthread_mutex_unlock(&lock);
        if (strcmp(colour, "Exit") == 0) { // If user types Exit then terminate
            running = 0; // Set variable to 0 to signal termination
            break;
        }
        pthread_mutex_lock(&lock);
        int ret = ioctl(fd, IOCTL_COMMAND, colour);
        pthread_mutex_unlock(&lock);
        if (ret < 0) {
            perror("IOCTL for changing colour failed");
        } else {
            printf("IOCTL command finished: %s\n", colour);
        }
    }
    close(fd);
    return(NULL);
}

// Function to display an image based on the received string
void display_image(const char *input) {
    if (strcmp(input, "Spry") == 0) {
        const char *image_path = "/home/user/Spry_Promo.jpg";
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "xdg-open %s", image_path);
        system(command);
        sleep(5);
        system("pkill -f %s", image_path);
        printf("Displayed image for: %s\n", input);
    }
}

// Function for reading from the /proc file
void *read_proc_file(void *arg) {
    int fd = open(PROC_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /proc file");
        return NULL;
    }
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\\0';
        printf("/proc content: %s\\n", buffer);
    } else {
        perror("Failed to read from /proc file");
    }
    close(fd);
    return NULL;
}

// Function for reading from the device
void *reader_thread(void *arg) {
    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device for reading");
        return NULL;
    }
    char buffer[BUFFER_SIZE];
    struct timeval start, end;
    while (running) { // Check termination
        memset(buffer, 0, BUFFER_SIZE);
        gettimeofday(&start, NULL);
        ssize_t bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
        gettimeofday(&end, NULL);
        long wait_time = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        if (bytes_read > 0) {
            printf("Received from kernel: %s (Waited: %ld ms)\n", buffer, wait_time);
        } else {
            perror("Failed while trying to read from device");
        }
        sleep(2);
    }
    close(fd);
    return NULL;
}

// Function for writing back to the device
void *writer_thread(void *arg) {
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device for writing");
        return NULL;
    }
    char response[] = "Processed successfully";
    struct timeval start, end;
    while (running) { // Check termination
        gettimeofday(&start, NULL);
        write(fd, response, strlen(response));
        gettimeofday(&end, NULL);
        long wait_time = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        printf("Sent response to kernel: %s (Waited: %ld ms)\n", response, wait_time);
        sleep(3);
    }
    close(fd);
    return NULL;
}

int main() {
    printf("Starting user-space application...\n");

    pthread_t readers[NUM_THREADS], writers[NUM_THREADS], ioctals[NUM_THREADS], proc_readers[NUM_THREADS];

    pid_t pid = fork();
    if (pid < 0) {
        perror("Failed while forking");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        for (int i = 0; i < NUM_THREADS; i++) {\n"
            pthread_create(&readers[i], NULL, reader_thread, NULL);
            pthread_create(&ioctals[i], NULL, perform_ioctl, NULL);
            pthread_create(&proc_readers[i], NULL, read_proc_file, NULL);
        }\n"
        for (int i = 0; i < NUM_THREADS; i++) {\n"
            pthread_join(readers[i], NULL);\n"
            pthread_join(ioctals[i], NULL);\n"
            pthread_join(proc_readers[i], NULL);\n"
        }\n"
    } else {
        for (int i = 0; i < NUM_THREADS; i++) {\n"
            pthread_create(&writers[i], NULL, writer_thread, NULL);\n"
        }\n"
        for (int i = 0; i < NUM_THREADS; i++) {\n"
            pthread_join(writers[i], NULL);\n"
        }\n"
        wait(NULL);\n"
    }
    return 0;
}
