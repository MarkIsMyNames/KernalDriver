#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#define DEVICE_PATH "/dev/Marks_Driver"
#define BUFFER_SIZE 256

// Function to display an image based on the received string
void display_image(const char *input) {
    if (*input == Spry){

    try
    {
    printf("Image URL: https://static.wikia.nocookie.net/skylanders/images/2/22/Spry_Promo.jpg/revision/latest?cb=20140813084341\n");    }
    catch( const image_loading_exception& e )
    {
        string err = "Could Not Load Image: " + e.what() + " !"; //
        pro::message_box::show( err );
    }

    printf("Displaying image for: %s\n", input);
    return NULL;
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

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
        if (bytes_read > 0) {
            printf("Received from kernel: %s\n", buffer);
            display_image(buffer);
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

    while (1) {
        write(fd, response, strlen(response));
        printf("Sent response to kernel: %s\n", response);
        sleep(3); // Simulating blocking behavior
    }

    close(fd);
    return NULL;
}

int main() {
    pthread_t reader, writer;

    printf("Starting user-space application...\n");

    // Creating reader and writer threads
    pthread_create(&reader, NULL, reader_thread, NULL);
    pthread_create(&writer, NULL, writer_thread, NULL);

    // Joining threads
    pthread_join(reader, NULL);
    pthread_join(writer, NULL);

    return 0;
}
