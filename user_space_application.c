#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <wait.h>
#include <sys/ioctl.h>

#define DEVICE_PATH "/dev/Marks_Driver"
#define BUFFER_SIZE 256
#define NUM_THREADS 3
#define IOCTL_MAGIC 'C'
#define IOCTL_COMMAND _IOW(IOCTL_MAGIC, 1, unsigned char) //Uses IOW as with this function we are writing to the kernel




pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

unsigned char colour;

//ioctal function for changing the colour of the portal
void perform_ioctl(int fd){
    int ret = ioctl(fd, IOCTL_COMMAND, &colour);
    if (ret < 0){
    perror("IOCTL for changing colour failed\n");
    }else{
    printf("IOCTL command finished\n");
    }
}



// Function to display an image based on the received string
void display_image(const char *input) {
    if (strcmp(input, "Spry") == 0) {
    printf("Image URL: https://static.wikia.nocookie.net/skylanders/images/2/22/Spry_Promo.jpg/revision/latest?cb=20140813084341\n");

        int image_loaded = 0; // 0 means failure, 1 means success

        if (!image_loaded) {
            printf("Image failed to load\n");
            return;
        }

        printf("Displaying image for: %s\n", input);
    }
}

// Function for reading from the device
void *reader_thread(void *arg) {
    int fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed while trying to open device");
        return NULL;
    }

    perform_ioctl(fd); // have to do the ioctl before reading



    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        pthread_mutex_lock(&lock); //so no other threads can access causing conflict.
        bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
        pthread_mutex_unlock(&lock);//unlocking to allow back access

        if (bytes_read > 0) {
            printf("Received from kernel: %s\n", buffer);

            display_image(buffer);

        } else {
          perror("Failed while trying to read from device");

        }
        sleep(2); // Simulating blocking behavior
    }

    close(fd);
    return NULL;
}

// Thread function for writing back to the device
void *writer_thread(void *arg) {
    int fd;
    char response[] = "Processed successfully";

    fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device for writing");
        return NULL;
    }

    perform_ioctl(fd); // same as above with writing


    while (1) {
        pthread_mutex_lock(&lock);
        write(fd, response, strlen(response));
        pthread_mutex_unlock(&lock);

        printf("Sent response to kernel: %s\n", response);
        sleep(3); // Simulating blocking behavior
    }

    close(fd);
    return NULL;
}

int main() {
    pthread_t readers[NUM_THREADS], writers[NUM_THREADS];

    printf("Please enter a colour for the portal (Format: R = Red): ");
    scanf(" %c", &colour);

    printf("Starting user-space application...\n");

    pid_t pid = fork();

    if (pid < 0) {
        perror("Failed while forking");
        exit(EXIT_FAILURE);
        //terminate/exit or catch
    }
    else if (pid == 0) { //child process handles reading
        for (int i = 0; i< NUM_THREADS; i++) { //x threads are created
            pthread_create(&readers[i], NULL, reader_thread, NULL); //useful if driver allows concurrent reads
        }
        for (int i = 0; i < NUM_THREADS; i++) { //separate loop to avoid waiting for each thread to finish before creating the next one
            pthread_join(readers[i], NULL);
        }
    }
    else {
        for (int i = 0; i < NUM_THREADS; i++) { //parent process handling writing
            pthread_create(&writers[i], NULL, writer_thread, NULL);
        }
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(writers[i], NULL);
        }
        wait(NULL); //wait is for processes, join() is for threads
                    //NULL tells us we don't want to know the exit status of the child
                    //We'd pass a pointer to an int instead of NULL if we wanted to see the exit status
    }

    return 0;
}
