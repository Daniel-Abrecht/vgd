/*
 * Copyright (C) 2017 Daniel Patrick Abrecht
 * 
 * This module is dual licensed under the MIT License and
 * the GNU General Public License v2.0
 */

#ifndef VGFB_H
#define VGFB_H

#include <linux/list.h>
#include <linux/fb.h>

#define VGFB_REFRESH_RATE 60lu

struct vgfbm {
	unsigned long count;
	unsigned long mem_count;
	struct platform_device * pdev;
	struct fb_info * info;
	struct fb_var_screeninfo old_var;
	struct fb_videomode videomode;
	int remap_signal;
	void * screen_base;
	void * next_screen_base;
};

ssize_t vgfb_read(struct fb_info *info, char __user *buf, size_t count, loff_t *ppos);
ssize_t vgfb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos);
int vgfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
int vgfb_realloc_screen(struct vgfbm* fb);
void vgfb_free_screen(struct vgfbm* fb);
int vgfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
int vgfb_set_par(struct fb_info *info);
int vgfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue, u_int transp, struct fb_info *info);
int vgfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info);

bool vgfb_acquire_mmap(struct vgfbm* fb);
void vgfb_release_mmap(struct vgfbm* fb);
bool vgfb_check_switch(struct vgfbm* fb);

int vgfb_create(struct vgfbm*);
void vgfb_remove(struct vgfbm*);
void vgfb_free(struct vgfbm*);

int vgfb_set_resolution(struct vgfbm*, const unsigned long[2]);

int vgfb_init(void);
void vgfb_exit(void);

#endif
