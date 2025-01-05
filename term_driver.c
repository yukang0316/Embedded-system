#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "term_driver"  // 디바이스 드라이버 이름
#define DEV_MAJOR_NUMBER 221      // 주번호
#define HIGH    1
#define LOW     0
#define SW0_IRQ 60
#define SW1_IRQ 61
#define SW2_IRQ 62
#define SW3_IRQ 63

int sw[4] = {4, 17, 27, 22};
int led[4] = {23, 24, 25, 1};
int led_state[4] = {0, };
int current_mode = 0;

struct task_struct *thread_id = NULL;
static struct timer_list timer;
static struct timer_list sequence_timer;
static int irq_numbers[4];
static int manual_flag = 0;
static int timer_flag = 0;

// 파일 연산 함수 선언
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

// 파일 연산 구조체 정의
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

// Timer callback function
static void timer_cb(struct timer_list *timer) {
    int ret, i;
    printk(KERN_INFO "Timer callback function!\n");
    
    if(timer_flag == 0){
        for(i=0;i<4;i++){
            ret = gpio_direction_output(led[i],HIGH);
        }
        timer_flag = 1;
    } else {
        for(i=0;i<4;i++){
            ret = gpio_direction_output(led[i],LOW);
        }
        timer_flag = 0;
    }
    
    mod_timer(timer, jiffies + HZ * 2);
}

static void sequence_timer_cb(struct timer_list *t) {
    int i;
    static char value = 1;
    unsigned char temp = value;
    
    for(i = 0; i < 4; i++) {
        gpio_set_value(led[i], temp & 0x01);
        temp = temp >> 1;
    }
    
    value = value << 1;
    if(value == 0x10)
        value = 0x01;
    mod_timer(&sequence_timer, jiffies + HZ * 2);
}

static void manual_led(int switch_num) {
    if (!manual_flag) {
        return;
    }
    
    if (switch_num >= 0 && switch_num <= 2) {
        led_state[switch_num] = !led_state[switch_num];
        gpio_set_value(led[switch_num], led_state[switch_num]);
        printk(KERN_INFO "Manual mode: LED %d changed to %s\n", 
               switch_num, led_state[switch_num] ? "ON" : "OFF");
    }
}

irqreturn_t irq_handler(int irq, void *dev_id) {
    printk(KERN_ERR "Interrupt received: %d\n", irq);

    switch (irq) {
        case SW0_IRQ:
            printk(KERN_ERR "SW0: All LED mode\n");
            del_timer_sync(&sequence_timer); 
            del_timer_sync(&timer);
            
            if (!manual_flag) {
                timer_setup(&timer, timer_cb, 0);
                timer.expires = jiffies + HZ * 2;
                add_timer(&timer);
                printk(KERN_ERR "Timer started\n");
            } else {
                manual_led(0);
            }
            break;
            
        case SW1_IRQ:
            printk(KERN_ERR "SW1: Sequential LED mode\n");
            del_timer_sync(&timer);
            del_timer_sync(&sequence_timer); 
            
            if (!manual_flag) {
                timer_setup(&sequence_timer, sequence_timer_cb, 0);
                timer.expires = jiffies + HZ * 2;
                add_timer(&sequence_timer);
                printk(KERN_ERR "Timer started\n");
            } else {
                manual_led(1);
            }
            break;
            
        case SW2_IRQ:
            printk(KERN_ERR "SW2: Manual mode\n");
            del_timer_sync(&sequence_timer); 
            del_timer_sync(&timer);
            if (!manual_flag) {
                manual_flag = 1;
            } else {
                manual_led(2);
            }
            break;
            
        case SW3_IRQ:
            printk(KERN_ERR "SW3: Reset mode\n");
            manual_flag = 0;
            del_timer_sync(&sequence_timer); 
            del_timer_sync(&timer);
            
            {
                int i;
                for (i = 0; i < 4; i++) {
                    gpio_set_value(led[i], LOW);
                    led_state[i] = 0;
                }
            }
            break;
            
        default:
            return IRQ_NONE;
    }
    
    return IRQ_HANDLED;
}

// 파일 연산 함수 구현
static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "Device opened\n");
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    char message[32];
    int message_len;
    int errors = 0;
    
    message_len = sprintf(message, "Mode: %d, Manual: %d\n", current_mode, manual_flag);
    
    if (*offset >= message_len)
        return 0;
    
    if (len > message_len - *offset)
        len = message_len - *offset;
    
    if (copy_to_user(buffer, message + *offset, len))
        errors++;
    
    *offset += len;
    return (errors ? -EFAULT : len);
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    char command;
    
    if (copy_from_user(&command, buffer, 1))
        return -EFAULT;
        
    switch(command) {
        case '1':  // 전체 LED 모드
            irq_handler(SW0_IRQ, NULL);
            break;
        case '2':  // 순차 점멸 모드 
            irq_handler(SW1_IRQ, NULL);
            break;
        case '3':  // 수동 모드
            irq_handler(SW2_IRQ, NULL);
            break;
        case '4':  // 리셋
            irq_handler(SW3_IRQ, NULL);
            break;
    }
    
    return len;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "Device closed\n");
    return 0;
}

// 모듈 초기화 함수
static int __init term_project_init(void) {
    int ret, i;
    
    // 문자 디바이스 드라이버 등록
    ret = register_chrdev(DEV_MAJOR_NUMBER, DEVICE_NAME, &fops);
    if (ret < 0) {
        printk(KERN_ALERT "Failed to register character device\n");
        return ret;
    }
    
    // GPIO 설정
    for (i = 0; i < 4; i++) {
        ret = gpio_request(sw[i], "switch");
        if (ret < 0) {
            printk(KERN_ERR "GPIO request failed for switch %d\n", i);
            return ret;
        }
        ret = gpio_direction_input(sw[i]);
        if (ret < 0) {
            printk(KERN_ERR "Failed to set GPIO direction for switch %d\n", i);
            return ret;
        }
    }
    
    for (i = 0; i < 4; i++) {
        irq_numbers[i] = gpio_to_irq(sw[i]);
        ret = request_irq(irq_numbers[i], irq_handler, IRQF_TRIGGER_FALLING, "switch_irq", &sw[i]);
        if (ret) {
            printk(KERN_ERR "Failed to request IRQ for switch %d\n", i);
            return ret;
        }
    }
    
    for (i = 0; i < 4; i++) {
        ret = gpio_request(led[i], "LED");
        if (ret < 0)
            printk(KERN_INFO "LED gpio request failed!\n");
    }
    
    printk(KERN_INFO "LED control driver initialized\n");
    return 0;
}

// 모듈 종료 함수
static void __exit term_project_exit(void) {
    int i;

    // 문자 디바이스 드라이버 해제
    unregister_chrdev(DEV_MAJOR_NUMBER, DEVICE_NAME);
    
    del_timer_sync(&timer);
    
    // GPIO 및 IRQ 해제
    for (i = 0; i < 4; i++) {
        free_irq(irq_numbers[i], NULL);
        gpio_free(sw[i]);
    }
    
    for (i = 0; i < 4; i++)
        gpio_free(led[i]);
    
    printk(KERN_INFO "LED control driver removed\n");
}

module_init(term_project_init);
module_exit(term_project_exit);
MODULE_LICENSE("GPL");
