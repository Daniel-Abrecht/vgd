/*
 * Copyright (C) 2017 Daniel Patrick Abrecht
 *
 * This module is dual licensed under the MIT License and
 * the GNU General Public License v2.0
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "vgfbmx.h"
#include "vgfb.h"
#include "vg.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Daniel Patrick Abrecht");
MODULE_DESCRIPTION("Virtual graphics frame buffer driver, allows to dynamically allocate framebuffer devices. This is intended to allow container hypervisors to provide virtual displays to it's containers on the fly.");

struct vgfbmx {
	int major;
	dev_t dev;

	struct cdev *cdev;
	struct device *device;
	struct class *vgfb_class;
};

static struct vgfbmx vgfbmx;

bool vgfbm_acquire(struct vgfbm *vgfbm)
{
	unsigned long val = vgfbm->count + 1;

	if (!val)
		return false;
	vgfbm->count = val;
	return true;
}

void vgfbm_release(struct vgfbm *vgfbm)
{
	unsigned long val;

	mutex_lock(&vgfbm->lock);
	val = vgfbm->count;
	if (!val) {
		mutex_unlock(&vgfbm->lock);
		pr_crit("underflow; use-after-free\n");
		dump_stack();
		return;
	}
	val--;
	vgfbm->count = val;
	if (val) {
		mutex_unlock(&vgfbm->lock);
		return;
	}
	vgfb_free(vgfbm);
	mutex_unlock(&vgfbm->lock);
	kfree(vgfbm);
}

struct fb_info *vgfbm_get_info(struct vgfbm *vgfbm)
{
	struct fb_info *info;

	mutex_lock(&vgfbm->info_lock);
	info = vgfbm->info;
	if (info)
		atomic_inc(&info->count);
	mutex_unlock(&vgfbm->info_lock);
	return info;
}

void vgfbm_put_info(struct fb_info *info)
{
	if (!atomic_dec_and_test(&info->count))
		return;
	if (info->fbops->fb_destroy)
		info->fbops->fb_destroy(info);
}

int vgfbmx_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct vgfbm *vgfbm;

	pr_info("vgfbmx: device opened\n");

	vgfbm = kzalloc(sizeof(struct vgfbm), GFP_KERNEL);
	if (!vgfbm)
		return -ENOMEM;

	mutex_init(&vgfbm->lock);
	mutex_init(&vgfbm->info_lock);

	file->private_data = vgfbm;
	vgfbm_acquire(vgfbm);

	ret = vgfb_create(vgfbm);
	if (ret < 0) {
		vgfbm_release(vgfbm);
		return ret;
	}

	return 0;
}

ssize_t vgfbmx_read(struct file *file, char __user *buf, size_t count,
	loff_t *ppos)
{
	int ret;
	struct fb_info *info = vgfbm_get_info(file->private_data);

	if (!info)
		return -ENODEV;
	if (!lock_fb_info(info)) {
		ret = -ENODEV;
		goto end;
	}

	ret = vgfb_read(info, buf, count, ppos);
	unlock_fb_info(info);

end:
	vgfbm_put_info(info);
	return ret;
}

ssize_t vgfbmx_write(struct file *file, const char __user *buf, size_t count,
	loff_t *ppos)
{
	ssize_t ret;
	struct fb_info *info = vgfbm_get_info(file->private_data);

	if (!info)
		return -ENODEV;
	if (!lock_fb_info(info)) {
		ret = -ENODEV;
		goto end;
	}

	ret = vgfb_write(info, buf, count, ppos);
	unlock_fb_info(info);

end:
	vgfbm_put_info(info);
	return ret;
}

int vgfbmx_close(struct inode *inode, struct file *file)
{
	struct vgfbm *vgfbm = file->private_data;

	vgfb_remove(vgfbm);
	vgfbm_release(vgfbm);

	pr_info("vgfbmx: device closed\n");
	return 0;
}

int vgfbm_get_vscreeninfo_user(const struct fb_info *info,
	struct fb_var_screeninfo __user *var)
{
	struct fb_var_screeninfo v;

	v = info->var;
	if (copy_to_user(var, &v, sizeof(v)))
		return -EFAULT;
	return 0;
}

int vgfbm_set_vscreeninfo_user(struct fb_info *info,
	const struct fb_var_screeninfo __user *var)
{
	struct vgfbm *fb = *(struct vgfbm **)info->par;
	struct fb_var_screeninfo v;
	int ret;

	if (copy_from_user(&v, var, sizeof(v)))
		return -EFAULT;

	if (v.bits_per_pixel != 32)
		return -EINVAL;

	if (v.xoffset != 0)
		return -EINVAL;

	if (v.yoffset > v.yres)
		return -EINVAL;

	console_lock();
	if (!lock_fb_info(info)) {
		console_unlock();
		return -ENODEV;
	}

	mutex_lock(&fb->lock);
	ret = vgfb_set_resolution(fb, (unsigned long[]){v.xres, v.yres});
	mutex_unlock(&fb->lock);
	if (ret < 0)
		goto end;

	ret = vgfb_check_var(&v, info);
	if (ret < 0)
		goto end;

	info->var = v;
	vgfb_set_par(info);

end:
	unlock_fb_info(info);
	console_unlock();
	return 0;
}

int vgfbm_pan_display(struct fb_info *info,
	const struct fb_var_screeninfo __user *var)
{
	int ret;
	struct fb_var_screeninfo v;

	if (copy_from_user(&v, var, sizeof(v)))
		return -EFAULT;

	console_lock();
	if (!lock_fb_info(info)) {
		console_unlock();
		return -ENODEV;
	}
	ret = vgfb_pan_display(&v, info);
	unlock_fb_info(info);
	console_unlock();
	return ret;
}


int vgfbm_get_fscreeninfo_user(const struct fb_info *info,
	struct fb_fix_screeninfo __user *fix)
{
	struct fb_fix_screeninfo f;

	f = info->fix;
	if (copy_to_user(fix, &f, sizeof(f)))
		return -EFAULT;
	return 0;
}

long vgfbmx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int tmp;
	void __user *argp = (void __user *)arg;
	struct vgfbm *vgfbm = file->private_data;
	struct fb_info *info;

	info = vgfbm_get_info(vgfbm);
	if (!info)
		return -ENODEV;

	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		ret = vgfbm_get_vscreeninfo_user(info, argp);
		break;
	case FBIOPUT_VSCREENINFO:
		ret = vgfbm_set_vscreeninfo_user(info, argp);
		break;
	case FBIOGET_FSCREENINFO:
		ret = vgfbm_get_fscreeninfo_user(info, argp);
		break;
	case FBIOPUTCMAP:
		ret = -EINVAL;
		break;
	case FBIOGETCMAP:
		ret = -EINVAL;
		break;
	case FBIOPAN_DISPLAY:
		ret = vgfbm_pan_display(info, argp);
		break;
	case FBIO_CURSOR:
		ret = -EINVAL;
		break;
	case FBIOGET_CON2FBMAP:
		ret = -EINVAL;
		break;
	case FBIOPUT_CON2FBMAP:
		ret = -EINVAL;
		break;
	case FBIOBLANK:
		ret = 0;
		break;
	case VGFBM_GET_FB_MINOR:
		tmp = info->node;
		ret = copy_to_user(argp, &tmp, sizeof(int)) ? -EFAULT : 0;
		break;
	default:
		if (!lock_fb_info(info)) {
			ret = -ENODEV;
			break;
		}
		ret = vgfb_ioctl(info, cmd, arg);
		unlock_fb_info(info);
		break;
	}

	vgfbm_put_info(info);

	return ret;
}

int vgfbmx_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct fb_info *info = vgfbm_get_info(file->private_data);

	if (!info)
		return -ENODEV;
	if (!lock_fb_info(info)) {
		ret = -ENODEV;
		goto end;
	}
	ret = vgfb_mmap(info, vma);
	unlock_fb_info(info);
end:
	vgfbm_put_info(info);
	return ret;
}

const struct file_operations vgfbmx_opts = {
	.owner = THIS_MODULE,
	.open = vgfbmx_open,
	.release = vgfbmx_close,
	.unlocked_ioctl = vgfbmx_ioctl,
	.read = vgfbmx_read,
	.write = vgfbmx_write,
	.mmap = vgfbmx_mmap,
};

int __init vgfbmx_init(void)
{
	int ret;
	dev_t dev_major;

	pr_info("vgfbmx: Initializing device\n");

	vgfbmx.cdev = cdev_alloc();
	if (!vgfbmx.cdev) {
		pr_err("vgfbmx: Failed to allocate cdev\n");
		ret = -ENOMEM;
		goto failed;
	}

	vgfbmx.cdev->ops = &vgfbmx_opts;
	vgfbmx.cdev->owner = THIS_MODULE;
	ret = alloc_chrdev_region(&dev_major, 0, 1, "vgfbmx");
	if (ret) {
		pr_err("vgfbmx: Failed to register chrdev\n");
		goto failed_after_cdev_alloc;
	}
	vgfbmx.major = MAJOR(dev_major);
	vgfbmx.dev = MKDEV(vgfbmx.major, 0);

	vgfbmx.vgfb_class = class_create(THIS_MODULE, "vgfb");
	if (!vgfbmx.vgfb_class) {
		pr_err("vgfbmx: Failed to create class vgfb\n");
		goto failed_after_alloc_chrdev_region;
	}

	vgfbmx.device = device_create(vgfbmx.vgfb_class, 0, vgfbmx.dev, 0,
				"vgfbmx");
	if (!vgfbmx.device) {
		pr_err("vgfbmx: Failed to create device\n");
		goto failed_after_class_create;
	}

	ret = cdev_add(vgfbmx.cdev, vgfbmx.dev, 1);
	if (ret) {
		pr_err("vgfbmx: Failed to add cdev\n");
		goto failed_after_device_create;
	}

	pr_info("vgfbmx: Initialised, device major number: %d\n",
		vgfbmx.major);

	ret = vgfb_init();
	if (ret) {
		pr_err("vgfbmx: vgfb_init failed\n");
		goto failed_after_device_create;
	}

	return 0;

failed_after_device_create:
	device_destroy(vgfbmx.vgfb_class, vgfbmx.dev);
failed_after_class_create:
	class_destroy(vgfbmx.vgfb_class);
failed_after_alloc_chrdev_region:
	unregister_chrdev_region(vgfbmx.dev, 1);
failed_after_cdev_alloc:
	cdev_del(vgfbmx.cdev);
failed:
	return ret ? ret : -1;
}

void __exit vgfbmx_exit(void)
{
	pr_info("vgfbmx: Unloading device\n");

	vgfb_exit();

	device_destroy(vgfbmx.vgfb_class, vgfbmx.dev);
	class_destroy(vgfbmx.vgfb_class);
	unregister_chrdev_region(vgfbmx.dev, 1);
	cdev_del(vgfbmx.cdev);
}

module_init(vgfbmx_init);
module_exit(vgfbmx_exit);
