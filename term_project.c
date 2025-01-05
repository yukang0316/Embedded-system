#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>

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
    
    // 현재 패턴에 따라 LED 설정
    for(i = 0; i < 4; i++) {
        gpio_set_value(led[i], temp & 0x01);
        temp = temp >> 1;
    }
    
    // 다음 패턴 준비
    value = value << 1;
    if(value == 0x10)  // 패턴이 0001 0000이 되면
        value = 0x01;   // 0000 0001로 리셋
    
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

// Switch interrupt handler
irqreturn_t irq_handler(int irq, void *dev_id) {
    printk(KERN_ERR "Interrupt received: %d\n", irq);  // 어떤 인터럽트가 발생했는지 확인

    switch (irq) {
        case SW0_IRQ:  // 전체 모드
            printk(KERN_ERR "SW0: All LED mode\n");
            
            // 이전 상태 정리
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
            
        case SW1_IRQ:  // 개별 모드
            printk(KERN_ERR "SW1: Sequential LED mode\n");
            
            // 이전 상태 정리
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
            
        case SW2_IRQ:  // 수동 모드
            printk(KERN_ERR "SW2: Manual mode\n");
            
            del_timer_sync(&sequence_timer); 
            del_timer_sync(&timer);
            if (!manual_flag) {
                manual_flag = 1;
            } else {
                manual_led(2);
            }
            break;
            
        case SW3_IRQ:  // 리셋 모드
            printk(KERN_ERR "SW3: Reset mode\n");
            
            manual_flag = 0;  // 수동 모드 해제
            
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
    
    printk(KERN_ERR "Interrupt handler completed\n");
    return IRQ_HANDLED;
}

// Module initialization
static int term_project_init(void) {
    int ret, i;
    
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
    for (i= 0; i< 4; i++) {
        ret = gpio_request(led[i], "LED");// 제어 권한
        if (ret < 0)
        printk(KERN_INFO "led_modulegpio_requestfailed!\n");
    }
            
    return 0;
}

// Module exit
static void term_project_exit(void) {
    int i;

    del_timer_sync(&timer);
    
    // Free switches and IRQs
    for (i = 0; i < 4; i++) {
        free_irq(irq_numbers[i], NULL);
        gpio_free(sw[i]);
    }
    
    for (i = 0; i < 4; i++)
        gpio_free(led[i]);
    
    printk(KERN_INFO "Module unloaded successfully\n");
}

module_init(term_project_init);
module_exit(term_project_exit);
MODULE_LICENSE("GPL");
