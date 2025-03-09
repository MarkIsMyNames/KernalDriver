#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#define DEVICE_PATH "/dev/Marks_Driver"
#define BUFFER_SIZE 256
#define NUM_THREADS 3
#define IOCTL_MAGIC 'C'
#define IOCTL_COMMAND _IOW(IOCTL_MAGIC, 1, char[32]) // Uses IOW to write a string to the kernel

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
char colour[32]; // Use a string instead of a single char

// ioctl function for changing the color of the portal
void perform_ioctl(int fd) {
    int ret = ioctl(fd, IOCTL_COMMAND, colour);
    if (ret < 0) {
        perror("IOCTL for changing colour failed");
    } else {
        printf("IOCTL command finished: %s\n", colour);
    }
}

// Function to display an image based on the received string
void display_image(const char *input) {
    if (strcmp(input, "Spry") == 0) {
        printf("Image URL: https://static.wikia.nocookie.net/skylanders/images/2/22/Spry_Promo.jpg/revision/latest?cb=20140813084341\n");
        printf("Displaying image for: %s\n", input);
    }
}

// Function for reading from the device
void *reader_thread(void *arg) {
    int fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    fd = open(DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed while trying to open device for reading");
        return NULL;
    }

    perform_ioctl(fd); // Perform ioctl after opening the device

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        pthread_mutex_lock(&lock); // Lock to prevent race conditions
        bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
        pthread_mutex_unlock(&lock); // Unlock after reading

        if (bytes_read > 0) {
            printf("Received from kernel: %s\n", buffer);
            display_image(buffer);
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
    int fd;
    char response[] = "Processed successfully";

    fd = open(DEVICE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open device for writing");
        return NULL;
    }

    perform_ioctl(fd); // Perform ioctl after opening the device

    while (1) {
        pthread_mutex_lock(&lock);
        write(fd, response, strlen(response));
        pthread_mutex_unlock(&lock);

        printf("Sent response to kernel: %s\n", response);
        sleep(3);
    }

    close(fd);
    return NULL;
}

int main() {
    pthread_t readers[NUM_THREADS], writers[NUM_THREADS];

    printf("Please enter a color for the portal (Format: 'red', 'blue', etc.): ");
    scanf("%31s", colour); // Allow full string input

    printf("Starting user-space application...\n");

    pid_t pid = fork();

    if (pid < 0) {
        perror("Failed while forking");
        exit(EXIT_FAILURE);
    } else if (pid == 0) { // Child process handles reading
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_create(&readers[i], NULL, reader_thread, NULL);
        }
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(readers[i], NULL);
        }
    } else { // Parent process handles writing
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_create(&writers[i], NULL, writer_thread, NULL);
        }
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(writers[i], NULL);
        }
        wait(NULL); // Wait for child process to finish
    }

    return 0;
}
