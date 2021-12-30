// Program intended to be used for testing dw-link
// Only boards with > 4K flash
#include <Arduino.h>
#include <unistd.h>

#define DETERMINISTIC

#define LOOSELEVEL 4 // try to loose starting with this round!
#define NOBLOCKLEVEL 6 // starting with this round, do not block any more!
#define RANDLEVEL 3 // starting with this level, make random moves for 1/5 of all moves 

#define X 1
#define O -1
#define EMPTY 0
#define NOKEY 0 
#define LEFTKEY 1
#define RIGHTKEY 2

int8_t board[9];
int8_t human;
int8_t turn = X;
int8_t move;
int8_t movenumber = 0;
int8_t gameround = 0;
bool computer_looses = false;
char key;

int8_t minimax(int8_t player);
int8_t chooseMove(void);
int8_t blockWin(void);
int8_t checkWon(void);
inline bool checkDraw(void);
inline bool checkTerminated(void);
int8_t readFieldNum();
boolean askAnotherGame(void);
boolean yesNoQuestion(void);
boolean leftOrRightQuestion(char left, char right);
int8_t readKey(char left, char right);
int8_t askMove(void);
void drawBoard(void);
void gosleep(void);
void error(int8_t num);
void initGame(void);

// evaluates board position when it is player's turn
// it minimizes for the human and maximizes for the computer
int8_t minimax(int8_t player)
{
  int8_t bestvalue, currvalue, currwin;

  currwin = checkWon();
  if (currwin == human || gameround > LOOSELEVEL) return -1*(10-movenumber);
  else if (currwin == -human) return (10-movenumber);
  else if (checkDraw()) return 0;
  else { 
    if (player == human  || gameround > LOOSELEVEL)  // we want minimize!
      bestvalue = 10;
    else // we want to maximize
      bestvalue = -10; 
    for (int8_t move=0; move < 9; move ++) {
      if (board[move] == EMPTY) {
	board[move] = player;
	movenumber++;
	currvalue = minimax(-player);
	if (player == human  || gameround > LOOSELEVEL) {
	  if (currvalue < bestvalue) bestvalue = currvalue;
	} else {
	  if (currvalue > bestvalue) bestvalue = currvalue;
	}
	board[move] = EMPTY;
	movenumber--;
      }
    }
  }
  return bestvalue;
}


// returns one of the best moves 
// note that in the beginning only a draw is enforcible
// In order to make it more interesting, we also choose random moves
// in later games and after round 3, we try to enforce a loss!
int8_t chooseMove(void)
{
  int8_t best[9];
  int8_t bestvalue = -100;
  int8_t bestnum = 0;
  int8_t currvalue;
  int8_t randmove;
  int8_t blockmove;
  
  if (gameround > RANDLEVEL && rand()%10 > 7) {
    do {
      randmove = rand()%9;
    } while (board[randmove] != EMPTY);
    delay(2000);
    return randmove;
  }

  for (int currmove=0; currmove<9; currmove++) 
    if (board[currmove] == EMPTY) {
      board[currmove] = -human;
      movenumber++;
      currvalue = minimax(human);
      if (currvalue > bestvalue) {
	bestnum = 1;
	best[0] = currmove;
	bestvalue = currvalue;
      } else if (currvalue == bestvalue) {
	best[bestnum++] = currmove;
      }
      board[currmove] = EMPTY;
      movenumber--;
    }
  if (bestnum == 0) {
    error(2);
    return 0;
  } else {
    if (bestvalue < 0 && gameround <= NOBLOCKLEVEL)  // we loose anyway
      blockmove = blockWin();
    else 
      blockmove = -1;
    if (blockmove >= 0) return blockmove;
    else return (best[rand()%bestnum]);
  }
}

int8_t blockWin(void) // block one win move, if there is one
{
  int8_t m;
  int8_t won;
  for (m = 0; m < 9; m++) {
    if (board[m] == EMPTY) {
      board[m] = human;
      won = checkWon();
      board[m] = EMPTY;
      if (won == human) return m;
    }
  }
  return -1;
}


int8_t checkWon(void)
{
  for (int8_t i=0; i < 9; i = i+3)
    if (board[0+i] != EMPTY && board[0+i] == board[1+i] && board[1+i] == board[2+i]) 
      return(board[0+i]);
  for (int8_t j=0; j < 3; j++)
    if (board[0+j] != EMPTY && board[0+j] == board[3+j] && board[3+j] == board[6+j]) 
      return(board[0+j]);
  if (board[0] != EMPTY && board[0] == board[4] && board[4] == board[8]) return(board[0]);
  if (board[2] != EMPTY && board[2] == board[4] && board[4] == board[6]) return(board[2]);
  return(EMPTY);
}

inline bool checkDraw(void)
{
  if (movenumber == 9) return true;
  else return false;
}

inline bool checkTerminated(void)
{
  if (checkDraw()) return true;
  else if (checkWon() != EMPTY) return true;
  else return false;
}



int8_t readFieldNum()
{
  key = NOKEY;
  while (true) {
    if (key >= '1' && key <='9') return key - '0';
    if (Serial.available()) {
      key = Serial.read();
    }
  } 
}

boolean askAnotherGame(void)
{

  Serial.print(F("\n\rAnother game (Y/N)? "));
  return (yesNoQuestion());
}

boolean yesNoQuestion(void)
{
  return leftOrRightQuestion('Y', 'N');
}

boolean leftOrRightQuestion(char left, char right)
{
  char reply;
  while (true) {
    reply = readKey(left, right);
    if (reply == NOKEY) gosleep();
    else if (reply == RIGHTKEY) return false;
    else if (reply == LEFTKEY) return true;
  }
}

int8_t readKey(char left, char right)
{
  key = NOKEY;
  while (true) {
    key = toupper(key);
    if (key == left) {
      Serial.print((char)left);
      return LEFTKEY;
    }
    if (key == right) {
      Serial.print((char)right);
      return RIGHTKEY;
    }
    if (Serial.available()) {
      key = Serial.read();
    }
  } 
}


int8_t askMove(void)
{
  int8_t reply = NOKEY;

  Serial.print(F("\n\rYour move: "));
  reply = readFieldNum();
  return (reply - 1);
}

void drawBoard(void)
{
  for (byte i=0; i < 10; i += 3) {
    Serial.print(F("\n\r+---+---+---+"));
    if (i == 9) break;
    Serial.print(F("\n\r| "));
    for (byte j=i; j < i+3; j++) {
      switch(board[j]) {
      case EMPTY: Serial.print(F(" ")); break;
      case X: Serial.print(F("X")); break;
      case O: Serial.print(F("O")); break;
      }
      Serial.print(F(" | "));
    }
  }
  Serial.println();
}

void gosleep(void)
{
  Serial.println(F("\n\rBye"));
  gameround = 0;
  delay(3000);
}

void error(int8_t num)
{

  Serial.print(F("\n\rInternal error "));
  Serial.println(num);

  gosleep();
}

void setup(void)
{
#ifdef DETERMINISTIC
  srand(1);
#else
  srand(micros()+millis()+analogRead(0));
#endif
  initSerial();
}

void initGame(void)
{

  Serial.print(F("\n\rT I C T A C T O E"));
  Serial.print(F("\n\r=================\n\r"));
  Serial.print(F("\n\rDo you want to start? (Y/N): "));
  gameround = 1;
  if (yesNoQuestion()) 
   human = X;
  else
    human = O;
}

void loop(void)
{
  if (gameround == 0) initGame();
  if (gameround == 0) return;
  Serial.print(F("\n\rGame "));
  Serial.print(gameround);
  gameround++;

  if (human == X) Serial.print(F("\n\rYou start..."));
  else Serial.print(F("\n\rI start..."));
  if (gameround == 0) return;
  
  for (int8_t i=0; i< 9; i++) board[i] = EMPTY;
  movenumber = 0;
  drawBoard();
  turn = X;

  while (!checkTerminated()) {
    if (human == turn) {
      move = askMove();
      if (move < 0) gosleep();
      if (gameround == 0) return;
      if (board[move] != EMPTY) {
	Serial.print(move+1);
	continue;
      }
      board[move] = human;
    } else {
      delay(200);
      Serial.print(F("\n\rI am thinking ..."));
      delay(2000);

      move = chooseMove();

      Serial.print(F("\n\rI play: "));

      board[move] = -human;
    }
    Serial.print(move+1);
    drawBoard();
    turn = -turn;
    movenumber++;
  }

  if (checkWon() == human) {
    Serial.print(F("\n\rCongratulation, you won!"));
  } else if (checkWon() == -human) {

    Serial.print(F("\n\rI won!"));

  } else {
    Serial.print(F("\n\rThis is a draw."));
  }
  human = -human;

  if (!askAnotherGame()) gosleep();
  if (gameround == 0) return;
} 

void initSerial()
{
  Serial.begin(9600);
#ifdef __AVR_ATtiny1634__
  pinMode(1, INPUT_PULLUP);
#endif
#ifdef __AVR_ATmega328PB__
  pinMode(0, INPUT_PULLUP);
#endif
}
