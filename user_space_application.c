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

// ioctl function for changing the colour of the portal
void *perform_ioctl(void *arg) {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device FOR IOCTAL");
        return(NULL);
    }
    while (1){

        printf("\nPlease enter a colour for the portal (Format: 'red', 'blue', etc.): Or type `Exit`)");
        scanf("%31s", colour);

        if (strcmp(colour, "Exit") == 0) {
            break; //exits
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

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        pthread_mutex_lock(&lock); // Lock to prevent race conditions
        bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
        pthread_mutex_unlock(&lock); // Unlock after reading

        if (bytes_read > 0) {
            printf("Received from kernel: %s\n", buffer);

            //copy string from buffer into colour (detect skylander & auto change colour)
            strncpy(colour, buffer, sizeof(colour) - 1);
            colour[sizeof(colour) - 1] = '\0'; //safe null termination
            ioctl(fd, IOCTL_COMMAND, colour);
            display_image(buffer);
        } else {
            perror("Failed while trying to read from device");
        }
        sleep(2);
    }

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

    while (1) {
        pthread_mutex_lock(&lock);
        write(fd, response, strlen(response));
        pthread_mutex_unlock(&lock);

        printf("Sent response to kernel: %s\n", response);
        sleep(3);
    }

}

int main() {

    printf("Starting user-space application...\n");

    pthread_t readers[NUM_THREADS], writers[NUM_THREADS], ioctals[NUM_THREADS];

    pid_t pid = fork();

    if (pid < 0) {
        perror("Failed while forking");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) { // Child process handles reading + ioctal function
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_create(&readers[i], NULL, reader_thread, NULL);
            pthread_create(&ioctals[i], NULL, perform_ioctl, NULL);
        }
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(readers[i], NULL);
            pthread_join(ioctals[i], NULL);
        }
    }
    else { // Parent process handles writing
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
