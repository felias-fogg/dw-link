// Program intended to be used for testing dw-link

#include <stdio.h>
#include <string.h>
#include "src/picoUART.h"
#include "src/pu_print.h"

// #define DETERMINISTIC

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

inline bool checkDraw()
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
  char ch = NOKEY;
  
  while (true) {
    if (ch >= '1' && ch <='9') return ch - '0';
    ch = purx();
  } 
}

boolean askAnotherGame(void)
{

  prints_P(PSTR("\n\rAnother game (Y/N)? "));
  return (yesNoQuestion());
}

boolean yesNoQuestion()
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
  char ch = NOKEY;
  
  while (true) {
    ch = toupper(ch);
    if (ch == left) {
      putx(left);
      return LEFTKEY;
    }
    if (ch == right) {
      putx(right);
      return RIGHTKEY;
    }
    ch = purx();
  } 
}


int8_t askMove(void)
{
  int8_t reply = NOKEY;

  prints_P(PSTR("\n\rYour move: "));
  reply = readFieldNum();
  return (reply - 1);
}

void drawBoard(void)
{

  for (byte i=0; i < 10; i += 3) {
    prints_P(PSTR("\n\r+---+---+---+"));
    if (i == 9) break;
    prints_P(PSTR("\n\r| "));
    for (byte j=i; j < i+3; j++) {
      switch(board[j]) {
      case EMPTY: prints_P(PSTR(" ")); break;
      case X: prints_P(PSTR("X")); break;
      case O: prints_P(PSTR("O")); break;
      }
      prints_P(PSTR(" | "));
    }
  }
  prints("\n\r");

}

void gosleep()
{
  prints_P(PSTR("\n\rBye\n\r"));
  gameround = 0;
  delay(3000);
}

void error(int8_t num)
{

  prints_P(PSTR("Internal error "));
  prints(num + '0');

  gosleep();
}

void setup(void)
{
#ifdef DETERMINISTIC
  srand(1);
#else
  srand(micros()+millis()+analogRead(0));
#endif
}

void initGame(void)
{

  prints_P(PSTR("\n\rT I C T A C T O E"));
  prints_P(PSTR("\n\r=================\n\r"));
  prints_P(PSTR("\n\rDo you want to start? (Y/N): "));
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
  prints_P(PSTR("\n\rGame "));
  putx(gameround + '0');
  gameround++;

  if (human == X) prints_P(PSTR("\n\rYou start..."));
  else prints_P(PSTR("\n\rI start..."));
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
	putx(move+1+'0');
	continue;
      }
      board[move] = human;
    } else {
      delay(200);
      prints_P(PSTR("\n\rI am thinking ..."));
      delay(2000);

      move = chooseMove();

      prints_P(PSTR("\n\rI play: "));

      board[move] = -human;
    }
    putx(move+1+'0');
    drawBoard();
    turn = -turn;
    movenumber++;
  }

  if (checkWon() == human) {
    prints_P(PSTR("\n\rCongratulation, you won!"));
  } else if (checkWon() == -human) {

    prints_P(PSTR("\n\rI won!"));

  } else {
    prints_P(PSTR("\n\rThis is a draw."));
  }
  human = -human;

  if (!askAnotherGame()) gosleep();
  if (gameround == 0) return;
} 

