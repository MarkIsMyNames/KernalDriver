#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

//paths for driver, update these if they change
#define DEVICE_FILE      "/dev/Marks_Driver"
#define PROC_FILE        "/proc/MDriver/Marks_Driver"

//For ioctl functions
#define IOCTL_GET _IOR('L', 1, short)
#define IOCTL_SET _IOW('L', 2, short)

#define BUF_SIZE 512

pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER; // Mutex to synchronize input from multiple threads, used to ensure that the picker doesn't appear during a function

//function signatures for thread functions
void *blocking_read_thread(void *arg);
void *proc_read_thread(void *arg);
void *ioctl_thread(void *arg);

//Frontend function pickers
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
    int status;
    pthread_t tid;

    // repeat indefinately
    while (1) {
        pthread_mutex_lock(&input_lock); //locks input to avoid race conditions
        print_menu();   //Frontend function pickers

        //ensures that a valid option is picked
        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Invalid input\n");
            while (getchar() != '\n'); //clear input buffer
            pthread_mutex_unlock(&input_lock); //unlocks mutex if input is valid
            continue;
        }
        pthread_mutex_unlock(&input_lock); //unlocks after right input

        //switch case to pick function
        switch (choice) {
            case 1: //read
                //create new thread
                status = pthread_create(&tid, NULL, blocking_read_thread, NULL);

                //check for  error creating thread
                if (status != 0) {
                    fprintf(stderr, "Error creating read thread: %s\n", strerror(status));
                } else {
                    pthread_detach(tid); //detaches to avoid a memory leak
                }
                break;

            case 2: //proc read
                //create new thread
                status = pthread_create(&tid, NULL, proc_read_thread, NULL);

                //check for  error creating thread
                if (status != 0) {
                    fprintf(stderr, "Error creating proc read thread: %s\n", strerror(status));
                } else {
                    pthread_detach(tid);//detaches to avoid a memory leak
                }
                break;

            case 3: //IOCTL Get

                //create new thread
                status = pthread_create(&tid, NULL, ioctl_thread, (void *)(intptr_t)1);

                //check for  error creating thread
                if (status != 0) {
                    fprintf(stderr, "Error creating IOCTL GET thread: %s\n", strerror(status));
                } else {
                    pthread_detach(tid);    //detaches to avoid a memory leak
                }
                break;

            case 4: //IOCTL set

                //create new thread
                status = pthread_create(&tid, NULL, ioctl_thread, (void *)(intptr_t)2);

                //check for  error creating thread
                if (status != 0) {
                    fprintf(stderr, "Error creating IOCTL SET thread: %s\n", strerror(status));
                } else {
                    pthread_detach(tid);    //detaches to avoid a memory leak
                break;


            case 5: //exit
                printf("Exiting...\n");
                pthread_mutex_destroy(&input_lock); //destroy the mutex to clean up resources
                exit(0);

            default:    //Invalid option entered
                printf("Invalid option. Please try again.\n");
        }
        sleep(1); //readability delay
    }

    return 0;
}


// thread function for blocking reading from device file
void *blocking_read_thread(void *arg) {
    (void)arg; //just used to acknowledge that arg is not used
    int fd; //file descripter
    char buf[BUF_SIZE];
    ssize_t bytes_read;

    printf("[Blocking Read Thread] Opening the device file: %s\n", DEVICE_FILE);
    fd = open(DEVICE_FILE, O_RDONLY);   //open file in read only mode

    //Ensures that the file could be opened
    if (fd < 0) {
        perror("[Blocking Read Thread] Error opening device file");
        pthread_exit(NULL); //exit thread
    }

    printf("[Blocking Read Thread] Waiting for data (blocking read)...\n");

    //read and ensure that read is sucessful
    bytes_read = read(fd, buf, sizeof(buf)-1);
    if (bytes_read < 0) {
        perror("[Blocking Read Thread] Error reading from device");
        close(fd);     //close file free up resources
        pthread_exit(NULL);//exit thread
    }

    buf[bytes_read] = '\0'; // null-terminating the string
    printf("[Blocking Read Thread] Read %zd bytes from device:\n%s\n", bytes_read, buf);

    close(fd);     //close file free up resources
    pthread_exit(NULL); //exit thread
}

void *proc_read_thread(void *arg) {
    (void)arg; //just used to acknowledge that arg is not used
    int fd; //file descripter
    char buf[BUF_SIZE];
    ssize_t bytes_read;

    printf("[Proc Read Thread] Opening the proc file: %s\n", PROC_FILE);
    fd = open(PROC_FILE, O_RDONLY);   //open file in read only mode

    //Ensures that the file could be opened
    if (fd < 0) {
        perror("[Proc Read Thread] Error opening proc file");
        pthread_exit(NULL);//exit thread
    }

    //read and ensure that read is sucessful
    bytes_read = read(fd, buf, sizeof(buf)-1);
    if (bytes_read < 0) {
        perror("[Proc Read Thread] Error reading from proc file");
        close(fd);     //close file free up resources
        pthread_exit(NULL);//exit thread
    }

    buf[bytes_read] = '\0';// null-terminating the string
    printf("[Proc Read Thread] Content read from proc file:\n%s\n", buf);

    close(fd);     //close file free up resources
    pthread_exit(NULL);//exit thread
}

void *ioctl_thread(void *arg) {
    int option = (int)(intptr_t)arg; //type casting the arg to intptr_t
    int fd; //file descripter
    int status; //for error handeling
    short button_status;
    short new_status;

    printf("[IOCTL Thread] Opening device file for IOCTL operations: %s\n", DEVICE_FILE);
    fd = open(DEVICE_FILE, O_RDWR);   //open file in read only mode

    //Ensures that the file could be opened
    if (fd < 0) {
        perror("[IOCTL Thread] Error opening device file");
        pthread_exit(NULL);
    }

    //check which Ioctl function was picked
    if (option == 1) {

        //call for Ioctl function to get button
        status = ioctl(fd, IOCTL_GET, &button_status);
        if (status < 0) {
            perror("[IOCTL Thread] IOCTL_GET failed");
        } else {
            printf("[IOCTL Thread] IOCTL_GET: Current button status: %d\n", button_status);
        }
    } else if (option == 2) {
        printf("[IOCTL Thread] Enter new button status (number): ");

        pthread_mutex_lock(&input_lock);    //lock mutex

        //check for a valid button input
        if (scanf("%hd", &new_status) != 1) {
            fprintf(stderr, "[IOCTL Thread] Invalid input for button status\n");

            while (getchar() != '\n');  //get characters still in the buffer

            pthread_mutex_unlock(&input_lock);  //unlock mutex
            close(fd);     //close file free up resources
            pthread_exit(NULL);     //exit thread
        }
        while (getchar() != '\n');      //get characters still in the buffer
        pthread_mutex_unlock(&input_lock);      //unlock mutex

        //ensure ioctl functions sucessfully completed
        status = ioctl(fd, IOCTL_SET, &new_status);
        if (status < 0) {
            perror("[IOCTL Thread] IOCTL_SET failed");
        } else {
            printf("[IOCTL Thread] IOCTL_SET: Changed button status to: %d\n", new_status);
        }

        //for unknown ioctl function
    } else {
        printf("[IOCTL Thread] Unknown IOCTL option provided.\n");
    }

    close(fd);     //close file free up resources
    pthread_exit(NULL);//exit thread
}
