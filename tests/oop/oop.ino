// Program intended to be used for testing dw-link
// Only for baords > 2K
#include <Arduino.h>

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
    Serial.print('(');
    Serial.print(x);
    Serial.print(',');
    Serial.print(y);
    Serial.print(')');
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

Circle c {1 , 2};
Rectangle r {10, 11, 5, 8};
Square s { 5, 5, 10};

void setup(void)
{
  Serial.begin(9600);
  Serial.println(); Serial.println();
  //Serial.print(F("c position: ")); c.printPos(); Serial.println();
  Serial.print(F("  area: ")); Serial.print(c.area()); Serial.println();
  Serial.print(F("r position: ")); r.printPos(); Serial.println();
  Serial.print(F("  area: ")); Serial.print(r.area()); Serial.println();
  Serial.print(F("s position: ")); s.printPos(); Serial.println();
  Serial.print(F("  area: ")); Serial.print(s.area()); Serial.println();

  Serial.println(F("Move s by +10, +10:"));
  s.move(10,10);
  Serial.print(F("s position: ")); s.printPos(); Serial.println();
  Serial.print(F("  area: ")); Serial.print(s.area()); Serial.println();

  Serial.println(F("Change size of c by 200%:"));
  c.sizeChange(200);
  Serial.print(F("c position: ")); c.printPos(); Serial.println();
  Serial.print(F("  area: ")); Serial.print(c.area()); Serial.println();

  Serial.println(F("Change size of s by 50%:"));
  s.sizeChange(50);
  Serial.print(F("s position: ")); s.printPos();  Serial.println();
  Serial.print(F("  area: ")); Serial.println(s.area()); 

  Serial.println(F("Change size of r by 50%:"));
  r.sizeChange(50);
  //  Serial.print(F("r position: ")); r.printPos();  Serial.println();
  Serial.print(F("  area: ")); Serial.println(r.area()); 
  Serial.println(F("============= END =============="));
}

void loop()
{
}
