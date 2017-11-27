#include <errno.h>
#include <linux/fb.h>
#include <linux/vg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QPixmap>
#include <QImage>
#include <QTimer>
#include <viewer.hpp>

FBViewer::FBViewer(QWidget *parent)
 : QMainWindow(parent)
{
  var = new struct fb_var_screeninfo();
  fb = -1;
  setWindowTitle("FB Viewer");
  label = new QLabel(this);
  scroll_area = new QScrollArea(this);
  scroll_area->setWidget(label);
  setCentralWidget(scroll_area);
  timer = new QTimer(this);
  timer->setInterval(1000/60);
  connect(timer, SIGNAL(timeout()), this, SLOT(update()));
  timer->start();
}

void FBViewer::resizeEvent(QResizeEvent* event){
  QMainWindow::resizeEvent(event);
  QSize ws = this->size();
  QSize ls = label->size();
  if( ls.width() > ws.width() || ls.height() > ws.height() ){
    scroll_area->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    scroll_area->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
  }else{
    scroll_area->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    scroll_area->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
  }
  if( var->xres != (unsigned long)ws.width() || var->yres != (unsigned long)ws.height() ){
    struct fb_var_screeninfo v = *var;
    v.xres = ws.width();
    v.yres = ws.height();
    ioctl(fb, FBIOPUT_VSCREENINFO, &v);
  }
  update();
}

void FBViewer::update(){
  if(!checkChanges())
    return;
  if(!memory_size)
    return;
  size_t offset = var->yoffset * var->xres_virtual * 4;
  label->setPixmap(QPixmap::fromImage(QImage(memory+offset,var->xres,var->yres,(QImage::Format)format)));
}

bool FBViewer::checkChanges(){
  struct fb_var_screeninfo var;
  if( ioctl(fb, FBIOGET_VSCREENINFO, &var) == -1 ){
    std::cerr << "FBIOGET_VSCREENINFO failed: " << strerror(errno) << std::endl;
    return false;
  }
  format = QImage::Format_RGBX8888; // TODO
  std::size_t size = var.xres_virtual * var.yres_virtual * 4;
  if( size != memory_size ){
    if( memory && memory != MAP_FAILED )
      munmap(memory,memory_size);
    memory_size = 0;
    memory = (unsigned char*)mmap(0, size, PROT_READ, MAP_SHARED, fb, 0);
    if( !memory || memory==MAP_FAILED ){
      std::cerr << "mmap failed: " << strerror(errno) << std::endl;
      return false;
    }
    memory_size = size;
  }
  if( var.xres != this->var->xres || var.yres != this->var->yres ){
    resize(var.xres,var.yres);
    label->resize(var.xres,var.yres);
  }
  *this->var = var;
  return true;
}

bool FBViewer::setFB(int new_fb){
  if(fb)
    ::close(fb);
  fb = new_fb;
  {
    int fb_minor;
    if( ioctl(fb, VGFBM_GET_FB_MINOR, &fb_minor) != -1 )
      std::cout << "Other framebuffer is /dev/fb" << fb_minor << std::endl;
  }
  if(!checkChanges())
    return false;
  update();
  return true;
}

int main(int argc, char **argv){
  int fd = -1;
  const char* tmp = argc >= 2 ? argv[1] : "/dev/vgfbmx";

  try {
    fd = std::stoi(tmp);
  } catch(std::invalid_argument& e) {
    fd = ::open(tmp,O_RDWR);
    if( fd == -1 ){
      std::cerr << "open failed: " << strerror(errno) << std::endl;
      return 1;
    }
  }

  QApplication app( argc, argv );

  FBViewer viewer;
  if(!viewer.setFB(fd))
    return 2;
  viewer.show();

  return app.exec();
}