#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    int fd;
    char write_buf[8];
    int led_num;
    
    fd = open("/dev/term_driver", O_RDWR);
    if (fd < 0) {
        printf("Failed to open device\n");
        return -1;
    }
    
    printf("LED Control Panel\n");
    for (int i = 1; i <= 4; i++) {
        printf("Mode %d: %d\n", i, i);
    }
    
    while (1) {
        printf("Type a mode: ");
        scanf("%d", &led_num);
        
        if (led_num == 5) {
            printf("Terminate the program\n");
            break;
        }
        
        if (led_num >= 0 && led_num <= 4) {
            sprintf(write_buf, "%d", led_num);
            write(fd, write_buf, strlen(write_buf));
            
            // led_num == 3인 경우 출력 형식을 변경
            if (led_num == 3) {
                while(led_num != 4){
                    printf("LED to enable: ");
                    scanf("%d", &led_num);
                    sprintf(write_buf, "%d", led_num);
                    write(fd, write_buf, strlen(write_buf));
                }
            }
            
        } else {
            printf("Invalid input. Please enter 0-5\n");
        }
    }
    
    close(fd);
    return 0;
}
