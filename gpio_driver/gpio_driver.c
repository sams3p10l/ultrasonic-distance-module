#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "defines.h"
#include "gpio_functions.h"

MODULE_LICENSE("Dual BSD/GPL");

// NOTE: Check Broadcom BCM8325 datasheet, page 91+
// NOTE: GPIO Base address is set to 0x7E200000,
//       but it is VC CPU BUS address, while the
//       ARM physical address is 0x3F200000, what
//       can be seen in pages 5-7 of Broadcom
//       BCM8325 datasheet, having in mind that
//       total system ram is 0x3F000000 (1GB - 16MB)
//       instead of 0x20000000 (512 MB)

/* Declaration of gpio_driver.c functions */
int gpio_driver_init(void);
void gpio_driver_exit(void);
static int gpio_driver_open(struct inode *, struct file *);
static int gpio_driver_release(struct inode *, struct file *);
static ssize_t gpio_driver_read(struct file *, char *buf, size_t , loff_t *);
static ssize_t gpio_driver_write(struct file *, const char *buf, size_t , loff_t *);

/* Structure that declares the usual file access functions. */
struct file_operations gpio_driver_fops =
{
    open    :   gpio_driver_open,
    release :   gpio_driver_release,
    read    :   gpio_driver_read,
    write   :   gpio_driver_write
};

/* Declaration of the init and exit functions. */
module_init(gpio_driver_init);
module_exit(gpio_driver_exit);

/* Global variables of the driver */
int counter = 0;
int enable = 1;

/* Major number. */
int gpio_driver_major;

struct hrtimer tajmer;
static ktime_t time_1_sec;

char* gpio_driver_buffer;

/* Virtual address where the physical GPIO address is mapped */
void* virt_gpio_base;

/* IRQ number. */
static int irq_gpio27 = -1;
static int irq_gpio22 = -1;

static enum hrtimer_restart stoperica(struct hrtimer *param)
{
	if(enable == 1)
	{
		counter ++;
		printk(KERN_INFO "Vrijeme: %d\n", counter);
	}
    else
	{
		printk(KERN_INFO "Stoperica je zaustavljena\n");
		counter = 0;
	}

    hrtimer_forward(&tajmer, ktime_get(), time_1_sec);

    return HRTIMER_RESTART;
}

/* interrupt handler called when falling edge on PB0 (GPIO_27) occurs*/
static irqreturn_t h_irq_gpio27(int irq, void *data)
{

    printk("Interrupt from IRQ 0x%x\n", irq);

    //value = GetGpioPinValue(GPIO_12);

    //printk("GPIO_12 level = 0x%x\n", value);

	if (enable == 2 || enable == 3)
	{
		enable = 1;
	}

	printk(KERN_INFO "STANJE JE NA %d.\n", enable);


    return IRQ_HANDLED;
}

/*
 * Initialization:
 *  1. Register device driver
 *  2. Allocate buffer
 *  3. Initialize buffer
 *  4. Map GPIO Physical address space to virtual address
 *  5. Initialize GPIO pins
 *  6. Initialize IRQ and register handler
 */
int gpio_driver_init(void)
{
    int result = -1;

    printk(KERN_INFO "Inserting gpio_driver module\n");

    /* Registering device. */
    result = register_chrdev(0, "gpio_driver", &gpio_driver_fops);
    if (result < 0)
    {
        printk(KERN_INFO "gpio_driver: cannot obtain major number %d\n", gpio_driver_major);
        return result;
    }

    gpio_driver_major = result;
    printk(KERN_INFO "gpio_driver major number is %d\n", gpio_driver_major);

    /* Allocating memory for the buffer. */
    gpio_driver_buffer = kmalloc(BUF_LEN, GFP_KERNEL);
    if (!gpio_driver_buffer)
    {
        result = -ENOMEM;
        goto fail_no_mem;
    }

    /* Initialize data buffer. */
    memset(gpio_driver_buffer, 0, BUF_LEN);

	/* Initialize high resolution timer. */

    hrtimer_init(&tajmer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    time_1_sec = ktime_set(TIMER_SEC, TIMER_NANO_SEC);
    tajmer.function = &stoperica;
    hrtimer_start(&tajmer, time_1_sec, HRTIMER_MODE_REL);

    /* map the GPIO register space from PHYSICAL address space to virtual address space */
    virt_gpio_base = ioremap(GPIO_BASE, GPIO_ADDR_SPACE_LEN);
    if(!virt_gpio_base)
    {
        result = -ENOMEM;
        goto fail_no_virt_mem;
    }

    /* Initialize GPIO pins. */
    SetGpioPinDirection(GPIO_17, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_27, GPIO_DIRECTION_OUT);

    /* Initialize gpio 27 ISR. */
    result = gpio_request_one(GPIO_27, GPIOF_IN, "irq_gpio27");
	if(result != 0)
    {
        printk("Error: GPIO request failed!\n");
        goto fail_irq;
    }
    irq_gpio27 = gpio_to_irq(GPIO_27);

	result = request_irq(irq_gpio27, h_irq_gpio27, IRQF_TRIGGER_FALLING, "irq_gpio27", (void *)(h_irq_gpio27));
	if(result != 0)
    {
        printk("Error: ISR not registered!\n");
        goto fail_irq;
    }

    return 0;

fail_irq:
    /* Unmap GPIO Physical address space. */
    if (virt_gpio_base)
    {
        iounmap(virt_gpio_base);
    }
fail_no_virt_mem:
    /* Freeing buffer gpio_driver_buffer. */
    if (gpio_driver_buffer)
    {
        kfree(gpio_driver_buffer);
    }
fail_no_mem:
    /* Freeing the major number. */
    unregister_chrdev(gpio_driver_major, "gpio_driver");

    return result;
}

/*
 * Cleanup:
 *  1. Release IRQ and handler
 *  2. release GPIO pins (clear all outputs, set all as inputs and pull-none to minimize the power consumption)
 *  3. Unmap GPIO Physical address space from virtual address
 *  4. Free buffer
 *  5. Unregister device driver
 */
void gpio_driver_exit(void)
{
    printk(KERN_INFO "Removing gpio_driver module\n");

    /* Release IRQ and handler. */
    disable_irq(irq_gpio27);
    free_irq(irq_gpio27, h_irq_gpio27);
    gpio_free(GPIO_27);

    disable_irq(irq_gpio17);
    free_irq(irq_gpio17, h_irq_gpio17);
    gpio_free(GPIO_17);

    /* Clear GPIO pins. */
    ClearGpioPin(GPIO_27);
	ClearGpioPin(GPIO_17);

	hrtimer_cancel(&tajmer);

    /* Set GPIO pins as inputs and disable pull-ups. */
    SetGpioPinDirection(GPIO_27, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_17, GPIO_DIRECTION_IN);

    /* Unmap GPIO Physical address space. */
    if (virt_gpio_base)
    {
        iounmap(virt_gpio_base);
    }

    /* Freeing buffer gpio_driver_buffer. */
    if (gpio_driver_buffer)
    {
        kfree(gpio_driver_buffer);
    }

    /* Freeing the major number. */
    unregister_chrdev(gpio_driver_major, "gpio_driver");
}

/* File open function. */
static int gpio_driver_open(struct inode *inode, struct file *filp)
{
    /* Initialize driver variables here. */

    /* Reset the device here. */

    /* Success. */
    return 0;
}

/* File close function. */
static int gpio_driver_release(struct inode *inode, struct file *filp)
{
    /* Success. */
    return 0;
}

/*
 * File read function
 *  Parameters:
 *   filp  - a type file structure;
 *   buf   - a buffer, from which the user space function (fread) will read;
 *   len - a counter with the number of bytes to transfer, which has the same
 *           value as the usual counter in the user space function (fread);
 *   f_pos - a position of where to start reading the file;
 *  Operation:
 *   The gpio_driver_read function transfers data from the driver buffer (gpio_driver_buffer)
 *   to user space with the function copy_to_user.
 */
static ssize_t gpio_driver_read(struct file *filp, char *buf, size_t len, loff_t *f_pos)
{
    /* Size of valid data in gpio_driver - data to send in user space. */
    int data_size = 0;

    if (*f_pos == 0)
    {
        /* Get size of valid data. */
        data_size = strlen(gpio_driver_buffer);

        /* Send data to user space. */
        if (copy_to_user(buf, gpio_driver_buffer, data_size) != 0)
        {
            return -EFAULT;
        }
        else
        {
            (*f_pos) += data_size;

            return data_size;
        }
    }
    else
    {
        return 0;
    }
}

/*
 * File write function
 *  Parameters:
 *   filp  - a type file structure;
 *   buf   - a buffer in which the user space function (fwrite) will write;
 *   len - a counter with the number of bytes to transfer, which has the same
 *           values as the usual counter in the user space function (fwrite);
 *   f_pos - a position of where to start writing in the file;
 *  Operation:
 *   The function copy_from_user transfers the data from user space to kernel space.
 */
static ssize_t gpio_driver_write(struct file *filp, const char *buf, size_t len, loff_t *f_pos)
{
    /* Reset memory. */
    memset(gpio_driver_buffer, 0, BUF_LEN);

    /* Get data from user space.*/
    if (copy_from_user(gpio_driver_buffer, buf, len) != 0)
    {
        return -EFAULT;
    }
    else
    {
        return len;
    }
}
