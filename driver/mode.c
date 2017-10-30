#include <linux/errno.h>
#include <linux/fb.h>
#include "mode.h"

const struct vgfb_mode*const*const vgfb_mode[] = {
#define X(X) &vgfb_mode_ ## X,
VGFB_MODES
#undef X
};

const unsigned vgfb_mode_count = sizeof(vgfb_mode)/sizeof(*vgfb_mode);

int vgfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct fb_var_screeninfo tmp = *var;

	if (tmp.bits_per_pixel != 24 && tmp.bits_per_pixel != 32)
	  return -EINVAL;

	if (tmp.xoffset != 0)
		return -EINVAL;

	if (tmp.yoffset > tmp.yres)
		return -EINVAL;

	if (!( tmp.xres == info->var.xres && tmp.yres == info->var.yres ))
	{
		struct list_head *it, *hit;
		bool found = false;
		list_for_each_safe (it, hit, &info->modelist) {
			struct fb_videomode* mode = &list_entry(it, struct fb_modelist, list)->mode;
			if (mode->xres == 0)
				continue;
			if (mode->xres == tmp.xres && mode->yres == tmp.yres)
			{
				found = true;
				continue;
			}
			list_del(it);
			kfree(it);
		}
		if(!found)
			return -EINVAL;
	}

	*var = info->var;

	var->xres = tmp.xres;
	var->yres = tmp.yres;
	var->xres_virtual = tmp.xres;
	var->yres_virtual = tmp.yres * 2;
	var->xoffset = tmp.xoffset;
	var->yoffset = tmp.yoffset;

	if (var->bits_per_pixel == 24) {
		var->red    = (struct fb_bitfield){ 0,8,0};
		var->green  = (struct fb_bitfield){ 8,8,0};
		var->blue   = (struct fb_bitfield){16,8,0};
		var->transp = (struct fb_bitfield){ 0,0,0};
	}else if (var->bits_per_pixel == 24) {
		var->red    = (struct fb_bitfield){ 0,8,0};
		var->green  = (struct fb_bitfield){ 8,8,0};
		var->blue   = (struct fb_bitfield){16,8,0};
		var->transp = (struct fb_bitfield){32,8,0};
	}

	return 0;
}

int vgfb_set_par(struct fb_info *info)
{
	return 0;
}

int vgfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue, u_int transp, struct fb_info *info)
{
	return -EINVAL;
}

int vgfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if( var->xoffset > info->var.xres_virtual - info->var.xres )
		return -EINVAL;
	if( var->yoffset > info->var.yres_virtual - info->var.yres )
		return -EINVAL;
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	return 0;
}