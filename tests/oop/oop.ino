// Program intended to be used for testing dw-link

#include <stdio.h>
#include <string.h>
#include "src/picoUART.h"
#include "src/pu_print.h"


char *convnum(long num)
{
  static char numstr[12];
  unsigned long div = 100000000UL;
  boolean nodigit = true;
  byte i = 0;

  if (num < 0) {
    num = -num;
    numstr[i++] = '-';
  }
  while (div != 0) {
    if (num/div == 0 && nodigit && div != 1) div /= 10;
    else {
      numstr[i++] = num/div + '0';
      num %= div;
      div /= 10;
      nodigit = false;
    }
  }
  numstr[i] = '\0';
  return numstr;
}


class TwoDObject {
public:
  int x, y;
  TwoDObject(int xini, int yini)
  {
    x = xini;
    y = yini;
  }

  void move(int xchange, int ychange)
  {
    x = x + xchange;
    y = y + ychange;
  }

  void printPos(void)
  {
    putx('(');
    prints(convnum(x));
    putx(',');
    prints(convnum(y));
    putx(')');
  }
};

class Circle : public TwoDObject {
public:
  int radius;
  Circle(int xini, int yini, int radiusini = 1) : TwoDObject(xini, yini) 
  {
    radius = radiusini;
  }

  int area(void)
  {
    return((int)((314L * radius * radius)/100));
  }

  void sizeChange(int percent)
  {
    radius = (int)(((long)radius*percent)/100);
  }

};
  
class Rectangle : public TwoDObject {
public:
  int height, width;

  Rectangle(int xini, int yini, int heightini=1, int widthini=1) : TwoDObject(xini, yini)
  {
    height = heightini;
    width = widthini;
  }

  int area(void)
  {
    return (width*height);
  }

  void sizeChange(int percent)
  {
    width = (int)(((long)width*percent)/100);
    height = (int)(((long)height*percent)/100);
  }
};

class Square : public Rectangle {
public:
  Square(int xini, int yini, int side) : Rectangle(xini, yini, side, side) { }
};

int main(void)
{
  Circle c {1 , 2};
  Rectangle r {10, 11, 5, 8};
  Square s { 5, 5, 10};

  prints_P(PSTR("\n\rc position: ")); c.printPos();
  prints_P(PSTR("  area: ")); prints(convnum(c.area())); prints_P(PSTR("\n\r"));
  prints_P(PSTR("r position: ")); r.printPos();
  prints_P(PSTR("  area: ")); prints(convnum(r.area())); prints_P(PSTR("\n\r"));
  prints_P(PSTR("s position: ")); s.printPos();
  prints_P(PSTR("  area: ")); prints(convnum(s.area())); prints_P(PSTR("\n\r"));
  prints_P(PSTR("Move s by +10, +10:\n\r"));
  s.move(10,10);
  prints_P(PSTR("s position: ")); s.printPos();
  prints_P(PSTR("  area: ")); prints(convnum(s.area())); prints_P(PSTR("\n\r"));
  prints_P(PSTR("Change size of c by 200%:\n\r"));
  c.sizeChange(200);
  prints_P(PSTR("c position: ")); c.printPos();
  prints_P(PSTR("  area: ")); prints(convnum(c.area())); prints_P(PSTR("\n\r"));
  prints_P(PSTR("Change size of s by 50%:\n\r"));
  s.sizeChange(50);
  prints_P(PSTR("s position: ")); s.printPos();
  prints_P(PSTR("  area: ")); prints(convnum(s.area())); prints_P(PSTR("\n\r"));
  prints_P(PSTR("============= END ==============\n\r"));
}
