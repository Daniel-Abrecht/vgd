#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "vgfb.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Daniel Patrick Abrecht");
MODULE_DESCRIPTION(
	"Virtual graphics frame buffer driver, allows to "
	"dynamically allocate framebuffer devices. "
	"This is intended to allow container hypervisors to "
	"provide virtual displays to it's containers on the fly."
);

struct vgfbmx {
	int major;
	dev_t dev;

	struct cdev * cdev;
	struct device * device;
	struct class * vgfb_class;
};

static struct vgfbmx vgfbmx;
static DEFINE_MUTEX(vgfb_list_mutex);
static LIST_HEAD(vgfb_list);

static int vgfbmx_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct vgfbm* vgfbm;

	printk(KERN_INFO "vgfbmx: device opened\n");

	vgfbm = kzalloc(sizeof(struct vgfbm), GFP_KERNEL);
	if (!vgfbm) {
		ret = -ENOMEM;
		goto failed;
	}
	file->private_data = vgfbm;

	mutex_lock(&vgfb_list_mutex);
	{
		size_t i = 0;
		struct list_head* it;
		list_for_each (it, &vgfb_list) {
			if (list_entry(it, struct vgfbm, list)->id != i)
				break;
			i++;
		}
		vgfbm->id = i;
		list_add(&vgfbm->list, it);
	}
	mutex_unlock(&vgfb_list_mutex);

	ret = vgfb_create(vgfbm);
	if (ret)
		goto failed_after_kzalloc;

	return 0;
	failed_after_kzalloc:
		kfree(vgfbm);
	failed:
		return ret;
}

static int vgfbmx_release(struct inode *inode, struct file *file)
{
	struct vgfbm* vgfbm = file->private_data;
	printk(KERN_INFO "vgfbmx: device closed\n");

	vgfb_destroy(vgfbm);

	mutex_lock(&vgfb_list_mutex);
	{
		struct list_head* it;
		list_for_each (it, &vgfb_list) {
			if (list_entry(it, struct vgfbm, list) != vgfbm)
				continue;
			list_del(it);
			break;
		}
	}
	mutex_unlock(&vgfb_list_mutex);

	kfree(vgfbm);

	return 0;
}

struct file_operations vgfbmx_opts = {
	.owner = THIS_MODULE,
	.open = vgfbmx_open,
	.release = vgfbmx_release
};

int __init vgfbmx_init(void)
{
	int ret;
	dev_t dev_major;

	printk(KERN_INFO "vgfbmx: Initializing device\n");

	vgfbmx.cdev = cdev_alloc();
	if (!vgfbmx.cdev) {
		printk(KERN_ERR "vgfbmx: Failed to allocate cdev\n");
		ret = -ENOMEM;
		goto failed;
	}

	vgfbmx.cdev->ops = &vgfbmx_opts;
	vgfbmx.cdev->owner = THIS_MODULE;
	ret = alloc_chrdev_region(&dev_major, 0, 1, "vgfbmx");
	if (ret) {
		printk(KERN_ERR "vgfbmx: Failed to register chrdev\n");
		goto failed_after_cdev_alloc;
	}
	vgfbmx.major = MAJOR( dev_major );
	vgfbmx.dev = MKDEV(vgfbmx.major,0);

	vgfbmx.vgfb_class = class_create(THIS_MODULE, "vgfb");
	if( !vgfbmx.vgfb_class ){
		printk(KERN_ERR "vgfbmx: Failed to create class vgfb\n");
		goto failed_after_alloc_chrdev_region;
	}

	vgfbmx.device = device_create(vgfbmx.vgfb_class, 0, vgfbmx.dev, 0, "vgfbmx");
	if( !vgfbmx.device ){
		printk(KERN_ERR "vgfbmx: Failed to create device\n");
		goto failed_after_class_create;
	}

	ret = cdev_add(vgfbmx.cdev, vgfbmx.dev, 1);
	if (ret) {
		printk(KERN_ERR "vgfbmx: Failed to add cdev\n");
		goto failed_after_device_create;
	}

	printk(KERN_INFO "vgfbmx: Initialised, device major number: %d\n",vgfbmx.major);

	ret = vgfb_init();
	if (ret){
		printk(KERN_ERR "vgfbmx: vgfb_init failed\n");
		goto failed_after_device_create;
	}

	return 0;

	failed_after_device_create:
		device_destroy(vgfbmx.vgfb_class,vgfbmx.dev);
	failed_after_class_create:
		class_destroy(vgfbmx.vgfb_class);
	failed_after_alloc_chrdev_region:
		unregister_chrdev_region(vgfbmx.dev, 1);
	failed_after_cdev_alloc:
		cdev_del (vgfbmx.cdev);
	failed:
		return ret ? ret : -1;
}

void __exit vgfbmx_exit(void)
{
	printk(KERN_INFO "vgfbmx: Unloading device\n");

	vgfb_exit();

	device_destroy(vgfbmx.vgfb_class,vgfbmx.dev);
	class_destroy(vgfbmx.vgfb_class);
	unregister_chrdev_region(vgfbmx.dev, 1);
	cdev_del(vgfbmx.cdev);
}

module_init(vgfbmx_init);
module_exit(vgfbmx_exit);