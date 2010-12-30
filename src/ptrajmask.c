/*  _______________________________________________________________________
 *
 *                        RDPARM/PTRAJ: 2008
 *  _______________________________________________________________________
 *
 *  This file is part of rdparm/ptraj.
 *
 *  rdparm/ptraj is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  rdparm/ptraj is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You can receive a copy of the GNU General Public License from
 *  http://www.gnu.org or by writing to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  CONTACT INFO: To learn who the code developers are, who to contact for
 *  more information, and to know what version of the code this is, refer
 *  to the CVS information and the include files (contributors.h && version.h)
 *
 */

//#include "contributors.h"
//#include "version.h"

/*  ________________________________________________________________________
 *
 *  This is code for an enhanced atom mask parser developed by Viktor Hornak
 *  at SUNY Stony Brook (Stony Brook University), in March of 2003.  Cheatham
 *  long sat on this very nice code that greatly extends the capabilities (and
 *  removes bugs) of the parser which turns atom masks into arrays representing
 *  whether an atom has been selected.  See the detailed comments below, but
 *  note that this new parser is now fully backward compatible with the Midas/Chimera
 *  style syntax previously employed.
 *  
 *  The new syntax adds expressions such as "and" (&) and "or" (|).  Note that
 *  this selection mechanism is done logically, such that a selection of
 *  ":1 & :2" does not mean both residues 1 and 2, but the logical "and" of :1
 *  and :2 which is NONE.  If you want both :1 and :2, the syntax would be
 *  ":1 | :2".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <math.h>

#define MASK_MODULE
/*#include "ptraj.h"*/
#include "ptrajmask.h"

/* 
 * this code takes an "atomic expression" loosely using Chimera/Midas syntax
 * and decomposes it into series of elementary actions that need to be done.
 * Parentheses and logical operators (precedence: ! > & > |) are allowed.
 * This is done through several intermediate stages: first, the atomic
 * expression is 'tokenized', i.e. 'elementary selections' are enclosed
 * to brackets [..], and basic error checking (e.g. for unknown symbols)
 * is done. Second, tokenized expression is converted into postfix 
 * notation (or Reverse Polish notation) which gets rid of parentheses
 * and defines the order of operations based on operator precedence.
 * Finally, postfix notation needs to be evaluated. This is done by
 * setting mask[] array to 'T' or 'F' for each atom.
 * Steps (2) and (3) are done through stack as described in one of the
 * Knuth's papers (as well as many other textbooks).
 *
 * The syntax for elementary selections is the following
 * :{residue numlist}      e.g. [:1-10] [:1,3,5] [:1-3,5,7-9]
 * :{residue namelist}     e.g. [:LYS] [:ARG,ALA,GLY]
 * @{atom numlist}         e.g. [@12,17] [@54-85] [@12,54-85,90]
 * @{atom namelist}        e.g. [@CA] [@CA,C,O,N,H]
 * @%{atom type namelist}  e.g. [@%CT] [@%N*,H] 
 * @/{element namelist}    e.g. [@/H] [@/C,H] (can be achieved as [@C*,H*])
 * Distance selection '<:', '>:', (residue based), and '<@', '>@', (atom based)
 *   You need to provide a reference structure (use "reference" command in ptraj) 
 *   for the coordinates of atoms to use this feature.
 *   e.g.  [:11-17 <@ 2.4]   all atoms within 2.4 A distance to :11-17
 * 
 * Wild characters:
 * '*' -- zero or more characters.
 * '?' -- one character.
 * '=' -- same as '*'
 * They can also be used in numerical environment.
 * :?0 means :10,20,30,40,50,60,70,80,90
 * :* means all residues and @* means all atoms
 *
 * The matching is case sensitive.
 *
 * compound expressions of the following type are allowed:
 * :{residue numlist | namelist}@{atom namelist | numlist | typelist}
 * and are processed as (i.e. replaced by two AND'ed expressions):  
 * :{residue numlist | namelist} & @{atom namelist | numlist | typelist}
 * e.g.  :1-10@CA    is equivalent to   :1-10 & @CA
 *       :LYS@/H     is equivalent to   :LYS & @/H
 *
 * more examples:
 * :ALA,TRP     ... all alanine and tryptophane residues
 * :5,10@CA     ... CA carbon in residues 5 and 10 
 * :* & !@/H    ... all non-hydrogen atoms (equivalent to "!@/H")
 * @CA,C,O,N,H  ... all backbone atoms
 * !@CA,C,O,N,H ... all non-backbone atoms (=sidechains for proteins only)
 * :1-500@O & !(:WAT | :LYS,ARG)
 *              ... all backbone oxygens in residues 1-500 but not in 
 *                  water, lysine or arginine residues
 * :LIG <: 5.0 & !@H= & !:LIG
 *              ... all heavy atoms in the residues within 5.0 A distance 
                    to :LIG but excluding :LIG
 *
 * Assumptions:
 * - residue, atom and atom type names:
 *   - residue names are 4 chars long, usually ending with ' '
 *   - atom names in AMBER are 4 chars long
 * - some static buffers during processing of maskString are set up 
 *   and this limits the length of selection string to MAXSELE (as defined in mask.h)
 * 
 * prnlev = 0   ... prints almost nothing
 * prnlev = > 5 ... prints original, tokenized, and rpn form of maskstring
 * prnlev = > 7 ... in addition prints mask array after eval() routine
 * 
 * TODO: (in order)
 * - code needs to be cleaned up to free allocated memory on exit
 * - LES copy selection (maybe [%1] [%1-3,7-9], etc.)
 *   '%' is chosen because '.' is taken for decimal point, and '#' may 
 *   eventually be used for model number as in Chimera
 * - Defining selections that could be used in later selections
 *   that would be convenient for backbone, sidechains, etc. but
 *   I don't know how to implement it yet; maybe {selection name}?
 *
 */

/* MODIFIED BY DAN ROE FOR USE AS A STAND_ALONE MODULE, Oct 2008 */

int prnlev;

/* from utility.c */
   void
pushStack( stackType **stackp, void *entry )
{
  stackType *sp;

  sp = malloc( (size_t) sizeof(stackType) );
  sp->entry = entry;
  sp->next = *stackp;
  *stackp = sp;
}

   void *
popStack( stackType **stackp )
{
  void *entry;
  stackType *sp;

  if ( *stackp == NULL ) {
    return( (char *) NULL );
  }

  sp = *stackp;
  entry = sp->entry;

  *stackp = sp->next;
  sp->next=NULL;
  free( sp );
  return( entry );
}

/*
 * freeStack()
 * Free up memory used by stack. 
 */
void freeStack(stackType *Stack) {
  /*char *pch;

  fprintf(stderr,"Freeing Stack:\n");
  while ( (pch=(char*) popStack(&Stack))!=NULL ) {
    fprintf(stderr,"  %c\n",*pch);
  }*/
  while ( popStack(&Stack)!=NULL ) {}

  return;
}

/*
 * freeStackEntry()
 * Free up memory used by stack. Also free up entries.
 */
void freeStackEntry(stackType *Stack) {
  char *pch;

  while ( (pch=(char*) popStack(&Stack))!=NULL ) {
    free(pch);
  }

  return;
}
  
/* ================================================================== */

int isOperator(char c) {
  /* allowed operators are: '!', '&', '|'
   * Note: '<','>' may be added in the future
   *       for distance based selection
   */
  
  if ( strchr("!&|<>", c) )
    return 1; /*true*/

  return(0);
}

int isOperand(char c) {
  /* this only checks if character 'c' is allowed
   * in operator: ':' is residue, '@' is atom, '*' is everything,
   * '/' is for atom element, '%' is for atom type, 
   * '-' for range, but also could be used in "Cl-", '+' is for "Na+", 
   * ',' for atom number enumeration, [a-zA-Z0-9] is for names and numbers 
   */

  if (strchr("*/%-?,'.=+", c) || isalnum(c))
    return 1; /*true*/
  
  return(0);     /* just to make compiler happy */
}
  
int priority(char op)
{ 
  /* define the priorities of operators */
  if (op == '>') return(6);
  if (op == '<') return(6);
  if (op == '!') return(5);
  if (op == '&') return(4);
  if (op == '|') return(3);
  if (op == '(') return(2);
  if (op == '_') return(1); 

  fprintf(stderr,"priority(): unknown operator ==%c== on stack when processing atom mask", op);
  return(0);
}

#undef ROUTINE
#define ROUTINE "tokenize()"

int tokenize(char *input, char *infix)
{
  /* preprocess the input string:
   *   1. remove spaces 
   *   2. isolate 'operands' into brackets [...]
   *   3. split expressions of the type :1-10@CA,CB into two parts;
   *      the two parts are joined with '&' operator and (for the sake
   *      of preserving precedence of other operators) enclosed into (..)
   *      :1-10@CA,CB    is split into  (:1-10 & @CA,CB)
   *   4. do basic error checking
   */
  
  char *p;
  char buffer[MAXSELE];
  int i, j, n, flag;

  flag = 0; /* 0 means new operand or operand was just completed, and terminated with ']', 
               1 means operand with ":" read,
               2 means operand with "@" read
               3 means '<' or '>' read, waiting for numbers.
            */
  i = 0; n = 0;  
  /* (*p) needs to scan the last '\0' as well so we cannot use
   * usual (*p != '\0') condition; rather we go one char more
   * beyond the length of the string */
  for (p = input; p <= input + strlen(input); p++) {
    if (isspace(*p)) /* gets rid of spaces and also of \n if any */
      continue;
    /* Distance operator.
       The two character should be together, no whitespace in between. 
       The operator will be kept in the [], selectElemMask() will skip if the first letter is '<' or '>'.
    */
    else if ( isOperator(*p) || strchr("()\0", *p)) {
      if (flag > 0) {
        buffer[n++] = ']'; 
        buffer[n++] = ')'; 
        buffer[n] = '\0';
        flag = 0;
        for (j = 0; j < n; j++) /* 'j < n' doesn't include \0 */
          infix[i++] = buffer[j];
        /* --------------------------------------------------- */
        /* now you have complete operand [...]  and you need to 
         * test if it's of the form [:..@..]. If it is, you need
         * to enclose it into (..) and split it into two via '&'
         * operator: [:1-5@CA,CB] becomes ([:1-5]&[@CA,CB]) */
        /*if ( buffer[1] == ':' && strchr(buffer, '@') ) {*/
        /* this expression has [:..@..] form and needs splitting */
        /*  infix[i++] = '(';
          for (j = 0; j < n; j++) {
            if ( buffer[j] == '@' ) {
              infix[i++] = ']'; infix[i++] = '&'; infix[i++] = '[';
            }
            infix[i++] = buffer[j];
          }
          infix[i++] = ')';
        } else { */ 
          /* expression doesn't need splitting, just copy it out */
        /*  for (j = 0; j < n; j++) // 'j < n' doesn't include \0 
              infix[i++] = buffer[j];
        }
         * --------------------------------------------------- */
      }
      infix[i++] = *p; 
      /*else if ( strchr("<>", *p) ) {  The last '\0' will be matched. */
      if ( *p == '>' || *p == '<' ) {
        n = 0;
        buffer[n++] = '('; 
        buffer[n++] = '[';
        buffer[n++] = *p; 
        p++; 
        buffer[n++] = *p; 
        flag = 3;
        if ( !strchr(":@", *p) ) {
          fprintf(stderr, "%c in wrong syntax\n", *(p-1));
          return 1;
        } 
      }
    } else if ( isOperand(*p) ) {
      if (flag == 0) {
        n = 0;
        buffer[n++] = '('; 
        buffer[n++] = '['; 
        flag = 1;
        if ( *p != '*') {
         fprintf(stderr, "wrong syntax: %c\n",*p);
         return 1;
        }
      }
      if (*p == '=') {  /* The new AMBER9 definition of wildcard '=' is equivalent to '*'. */
        if (flag > 0)
          *p = '*'; 
        else {
          fprintf(stderr, "'=' not in name list syntax\n");
          return 1;
        }
      }
      buffer[n++] = *p; 
    } else if ( *p == ':' ) {
      if (flag == 0) {
        n = 0;
        buffer[n++] = '('; 
        buffer[n++] = '['; 
        buffer[n++] = ':'; 
        flag = 1;
      } else {
        buffer[n++] = ']'; 
        buffer[n++] = ')'; 
        buffer[n++] = '|'; 
        buffer[n++] = '('; 
        buffer[n++] = '['; 
        buffer[n++] = ':'; 
        flag = 1;
      }
    } else if ( *p == '@' ) {
      if (flag == 0) {
        n = 0;
        buffer[n++] = '('; 
        buffer[n++] = '['; 
        buffer[n++] = '@'; 
        flag = 2;
      } else if (flag == 1) {
        buffer[n++] = ']'; 
        buffer[n++] = '&'; 
        buffer[n++] = '['; 
        buffer[n++] = '@'; 
        flag = 2;
      } else if (flag == 2) {
        buffer[n++] = ']'; 
        buffer[n++] = ')'; 
        buffer[n++] = '|'; 
        buffer[n++] = '('; 
        buffer[n++] = '['; 
        buffer[n++] = '@'; 
        flag = 2;
      }
    } else {
      fprintf(stderr, "Unknown symbol (%c) expression when parsing atom mask (%s)\n", 
	    *p, input);
      return 1;
    }
  } /* for p */

  /* operator should have at least 4 characters: [:1],[@C] */
  /* is this check worth it? */
  for (flag = 0, n = 0, p = infix; *p != '\0'; p++) {
    if ( *p == '[' ) {
      flag = 1;
      n++;
    } else if ( *p == ']' ) {
      if ( n < 3 && *(p-1) != '*' )
        fprintf(stderr,"Warning: \'%c\' empty token?\n", *(p-1));
      flag = 0;
      n = 0;
    } else {
      if (flag == 1)
        n++;
    }
  }
  
  /* add terminal symbol '_' - needed in next step */
  infix[i-1] = '_';  
  infix[i] = '\0';

  return 0;
} /* end tokenize */



int torpn(char *infix, char *postfix)
{
  /*
   * Convert tokenized string (infix) into postfix (RPN) notation 
   * 
   * 'infix' points to tokenized infix atom expression
   * 'postfix' should have rpn representation at the end
   *
   * First, a terminal symbol '_' is placed at the end of the string
   * (which was done in a routine tokenize(), i.e. previous step). We 
   * also push this symbol '_' onto the stack.
   * Then, the expression is processed according to the following rules:
   * - operands (part enclosed in [..]) are copied directly to 'postfix'
   * - left parentheses are always pushed onto the stack
   * - when a right parenthesis is encountered the symbol at the top
   *   of the stack is popped off the stack and copied to 'postfix'
   *   until the symbol at the top of the stack is a left parenthesis.
   *   When that occurs both parentheses are discarded
   * - if the symbol scanned from 'infix' has a higher precedence 
   *   then the symbol at the top of the stack, the symbol being 
   *   scanned is pushed onto the stack
   * - if the precedence of the symbol being scanned is lower than 
   *   or equal to the precedence of the symbol at the top of the 
   *   stack, the stack is popped to 'postfix' until the condition
   *   holds
   * - when the terminating symbol '_' is reached on the input scan
   *   the stack is popped to 'postfix' until the terminating symbol
   *   is also reached on the stack. Then the algorithm terminates.
   * - if the top of the stack is '(' and the terminating symbol '_'
   *   is scanned, or ')' is scanned when '_' is at the top of the
   *   stack, the parentheses of the atom expression were unbalanced
   *   and an unrecoverable error has occurred.
   *
   */

  char *p, *pp, *term;
  int i,P1,P2;
  int flag;

  stackType *Stack = NULL;
  p=NULL;

  /* push terminal symbol '_' to stack */
  term = (char *) malloc(sizeof(char)); 
  *term = '_';
  pushStack(&Stack,term);

  i = 0;
  flag = 0; /* 1 when start with "[", 0 when "]" is finished. */
  for (p = infix; *p != '\0'; p++) {
    /*if ( isOperand(*p) || strchr(":@", *p) ) {*/
    if (*p == '[') {
      postfix[i++] = *p;
      flag = 1;
    } else if (*p == ']') {
      postfix[i++] = *p;
      flag = 0;
    } else if ( flag ) {
      postfix[i++] = *p;
    } else if (*p == '(') {
      pushStack(&Stack,p);
    } else if (*p == ')') {
      while (*(pp = (char *)popStack(&Stack)) != '(') {
        if (*pp == '_') {
          fprintf(stderr,"parsing atom mask: unbalanced parentheses in expression\n");
          freeStack(Stack);
          free(term);
          return 1;
        }
        postfix[i++] = *pp;
      }
      /* at this point both parentheses are discarded */
    } else if (*p == '_') { 
      while (*(pp=(char *)popStack(&Stack)) != '_') {
        if (*pp == '(') { 
          fprintf(stderr,"parsing atom mask: unbalanced parentheses in expression\n");
          freeStack(Stack);
          free(term);
          return 1;
         }
        postfix[i++] = *pp;
      }
    } else if ( isOperator(*p) ) {
      P1=priority(*p);
      P2=priority(*((char*)((Stack)->entry)));
      if ((P1==0)||(P2==0)) {
        fprintf(stderr,"Error in priority.\n");
        freeStack(Stack);
        free(term);
        return 1;
      }
      if ( P1 > P2 )
        pushStack(&Stack,p);
      else {
        while ( P1 <= P2 ) {
          pp = (char *)popStack(&Stack);
          postfix[i++] = *pp;
          P1=priority(*p);
          P2=priority(*((char*)((Stack)->entry)));
          if ((P1==0)||(P2==0)) {
            fprintf(stderr,"Error in priority.\n");
            freeStack(Stack);
            free(term);
            return 1;
          }
        }
        pushStack(&Stack,p);
      }
    }
    else {
      fprintf(stderr,"parsing atom mask: unknown symbol (%c)\n", *p);
      freeStack(Stack);
      free(term);
      return 1;
    }
  }    
  postfix[i] = '\0';

  free((void *) term);
  return 0;

} /* end torpn */


char *
eval(char *postfix, int atoms, int residues, Name *atomName, 
     Name *residueName, int *ipres, void *X, char type)
{

  char *pToken;
  char buffer[MAXSELE];
  int i, j, numSelAtoms = 0;
  char *p, *pMask1, *pMask2, *pMask;

  /* 
   * elementary atom expressions are converted to mask character
   * arrays (mask[i] = 'T'|'F', i=1,natom) when they are pushed 
   * to a stack. In fact, just a pointer to this char array is pushed
   * onto the stack. Whenever an operator pops the stack the 
   * appropriate binary (or unary) operation is carried out and
   * the char array is freed up.
   *
   */

  stackType *Stack = NULL;
  i = 0;
  
  for (p = postfix; *p != '\0'; p++) {
    if (*p == '[')        /* 'operand' begins here */
      i = 0;
    else if (*p == ']') { /* 'operand' is completed */
      buffer[i] = '\0';
      pToken = (char *) malloc( (strlen(buffer)+1) * sizeof(char));
      strcpy(pToken, buffer);
      /* this code should also be ok if this is just a single expression,
       * i.e. first ']' is followed immediately by '\0' (end of string), 
       * and so no logical operators are contained in (*infix) */
      /* selectElemMask allocates char mask array to which pMask points */
      pMask = selectElemMask(pToken, atoms, residues, atomName, residueName, ipres); 
      pushStack(&Stack,pMask);
      free((void *) pToken);
    }
    else if ( isOperand(*p)||strchr(":@", *p))  /* operand is a part inside [...] */
      buffer[i++] = *p;
    else if (*p == '&' || *p == '|') {
      pMask1 = (char *)popStack(&Stack);
      pMask2 = (char *)popStack(&Stack);
      /* printf("[%s] %s [%s]\n", pMask2, (*p == '&') ? "AND" : "OR", pMask1); */
      /* binop performs the operation and returns the result in pMask
       * pMask array is allocated in binop and therefore you can release both
       * pMask1 and pMask2 after you return from binop()
       */
      if (pMask2 != NULL && pMask1 != NULL) {
        pMask = binop(*p, pMask2, pMask1, atoms);
        if (pMask==NULL) {
          fprintf(stderr,"Error: binary op failed.\b");
          freeStackEntry(Stack);
          return NULL;
        }
      } else {
        fprintf(stderr,"Error: illegal binary operation\n");
        freeStackEntry(Stack);
        return NULL;
      }
      pushStack(&Stack,pMask);

      free((void *) pMask1);
      free((void *) pMask2);
    } 
    else if ( strchr("<>", *p) ) {
      if(strchr(":@", *(p+1)) && *(p+1) != '\0') {
        buffer[i++] = *p;
      } else {
        pMask1 = (char *)popStack(&Stack); /* This should be the distance criteria like >@2.4 .*/
        pMask2 = (char *)popStack(&Stack);
        pMask = selectDist(pMask1, pMask2, atoms, residues, ipres, X, type);
        if (pMask==NULL) return NULL;
        pushStack(&Stack,pMask);

        free((void *) pMask1);
        free((void *) pMask2);
      }
    } 
    else if (*p == '!') {
      pMask1 = (char *)popStack(&Stack);
      /* printf("NEG [%s]\n", pMask1); */
      if (pMask1 != NULL) 
        pMask = neg(pMask1, atoms);
      else {
        fprintf(stderr,"Error: illegal unary neg operation\n");
        freeStackEntry(Stack);
        return NULL;
      }
      pushStack(&Stack,pMask);

    } else {
      printf("Error: unknown symbol while evaluating RPN/n");
      freeStackEntry(Stack);
      return NULL;
    }
  } /* for (p) */
 
  pMask = (char *)popStack(&Stack);      /* pMask should point to the resulting mask, but   */
                      /* this should also free up the last item on Stack */
                      /* If not, there must be missing operand. */
  if (Stack) {
    printf("Error: there might be missing operands in the mask.\n");
    freeStackEntry(Stack);
    free(pMask);
    return NULL;
  }
  for (j = 0; j < atoms; j++)
    if ( pMask[j] == 'T' ) 
      numSelAtoms++;

  if (prnlev > 7) {
    for (j = 0; j < atoms; j++) {
      if (j % 20 == 0) printf("\n%4d:  ", j+1);
      printf("%c,", pMask[j]);
    }
    printf("\n");
  }
  
  return(pMask);

} /* eval */


/* For :1@O <:5 means the residues whose atoms (any)within 5 A to :1@O 
   For :1@O >:5 means the residues whose atoms (any) greater than 5 A to :1@O 
   If you want residue which all its atoms greater than 5 A, use !(:1@O <:5) 
 */
char * selectDistd(char *criteria, char *center, int atoms, int residues, int *ipres, double *X) {
  int i, j, k, i3, j3;
  char *pMask;
  double dx, dy, dz, x2, y2, z2;
  char type; /* : or @*/
  char comp; /* < or > */
  double dist, distance;
  int curres;
  //int total_center_atom;
  
  comp = criteria[0];
  type = criteria[1];
  /* Was intended for picking up the atom/residue has a distance greater than threshold to ALL the atoms in the center[]. Not necessary now. 
  total_center_atom = 0;
  if (comp == '>') {  
    for (i = 0; i < atoms; i++)
      if (center[i] == 'T') total_center_atom++;
  }
  */
  if ( sscanf(criteria+2, "%lf", &dist) != 1) { 
    fprintf(stderr,"selectDistd: fail to read distance criteria %s.\n", criteria);
    return NULL;
  }
  
  pMask = (char *) malloc( atoms * sizeof(char));
  for (i = 0; i < atoms; i++)
    pMask[i] = 'F';
  
  curres = 0;
  for (i = 0; i < atoms; i++) {
    if (i >= ipres[curres+1]-1) curres++;
    if ( pMask[i] == 'T' ) continue;
    i3 = i * 3;
    for (j = 0; j < atoms; j++) {
      if (center[j] == 'F') continue;
      j3 = j * 3;
      dx = X[i3  ] - X[j3  ]; x2 = dx * dx;
      dy = X[i3+1] - X[j3+1]; y2 = dy * dy;
      dz = X[i3+2] = X[j3+2]; z2 = dz * dz;
      distance = sqrt(x2 + y2 + z2);
      if (type == ':') {
        if ( comp == '<') {
          if ( distance < dist ) {
            for (k = ipres[curres] - 1; k < ipres[curres+1]-1; k++) {
              pMask[k] = 'T';
            }
            i = ipres[curres+1]-2; /* go to the bottom of i-loop, then i++, i will be ipres[curres+1]-1.*/
            break;
          }
        } /*end of if ( comp == '<') */
        else if ( comp == '>') {
          if ( distance > dist ) {
            for (k = ipres[curres] - 1; k < ipres[curres+1]-1; k++) {
              pMask[k] = 'T';
            }
            i = ipres[curres+1]-2; /* go to the bottom of i-loop, then i++, i will be ipres[curres+1]-1.*/
            break;
          }
        } /*end of if ( comp == '>') */
        else {
          fprintf(stderr,"selectDistd: Unknown distance criteria %s.\n", criteria);
          free(pMask);
          return NULL;
        }
      } /* end of if (type == ':') */
      else if (type == '@') {
        if ( comp == '<' && distance < dist ) {
          pMask[i] = 'T';
          break;
        }  
        if ( comp == '>' && distance > dist ) {
          pMask[i] = 'T';
          break;
        }  
      }
      else {
        fprintf(stderr,"selectDistd: Unknown distance criteria %s.\n", criteria);
        free(pMask);
        return NULL;
      }
    }
  }
  
  return (pMask);
  
}

char * selectDistf(char *criteria, char *center, int atoms, int residues, int *ipres, float *X) {
/*  int i, j, k;
  char *pMask;
  float dx, dy, dz, x2, y2, z2;
  char type; // : or @
  char comp; // < or > 
  float dist, distance;
  int curres = 0;
*/  
  fprintf(stderr,"selectDistf: NO selectDistf %s.\n", criteria);
  /* Please copy from the selectDistd(). */
  return (NULL);
  
}

char * selectDist(char *criteria, char *center, int atoms, int residues, int *ipres, void *X, char type) {
  
  if ( X == NULL ) {
    fprintf(stderr,"selectDist(): No coordinate info for distance operator.\n");
    return NULL;
  }
  if ( type == 'f')
    return selectDistf(criteria, center, atoms, residues, ipres, X);
  else if (type == 'd')
    return selectDistd(criteria, center, atoms, residues, ipres, X);
  else {
    fprintf(stderr,"selectDist(): Unknown type of array.\n");
    return NULL;
  }
}

char * binop(char op, char *m2, char *m1, int atoms) {
  int i;
  char *pMask;

  /* we could avoid allocating a new char array for results here
   * by returning the result in m2[] (or m1[]) but creating a new
   * char array for results and freeing up m2[] and m1[] up in 
   * the calling routine is more straightforward and clearer */
  pMask = (char *) malloc( atoms * sizeof(char) );
  for (i = 0; i < atoms; i++)
    pMask[i] = 'F';
  
  switch (op) {
    case '&': 
      for (i = 0; i < atoms; i++)
        if (m2[i] == 'T' && m1[i] == 'T')
          pMask[i] = 'T';
      break;
    case '|':
      for (i = 0; i < atoms; i++)
        if (m2[i] == 'T' || m1[i] == 'T')
          pMask[i] = 'T';
      break;
    default:
      printf("Error: unknown operator ==%c==\n", op);
      free(pMask);
      return NULL;
  }
  return(pMask);
} /* binop */

char * neg(char *m1, int atoms) {
  int i;
  
  for (i = 0; i < atoms; i++)
    if (m1[i] == 'T') 
      m1[i] = 'F';
    else
      m1[i] = 'T';

  return (m1);
} /* neg */
  
  
/* 
 * the routines below deal with parsing elementary expressions,
 * which were obtained by the routines above (specifically the
 * last one of them  eval(char *postfix) )
 */

void 
resnum_select(int res1, int res2, char *mask, int residues, int *ipres) 
{
  int i, j;

  /* DEBUG 
  fprintf(stderr,"In resnum_select: res1=%i res2=%i residues=%i\n",res1,res2,residues);*/

  for (i = 0; i < residues; i++)
    if (i+1 >= res1 && i+1 <= res2) {
      /* DEBUG 
      fprintf(stderr,"  i=%i i+1=%i\n",i,i+1);*/
      for (j = ipres[i]; j < ipres[i+1]; j++)
        mask[j-1] = 'T';
    }
}

void 
resname_select(char *p, char *mask, int residues, Name *residueName, int *ipres) 
{
  int i,j;
  char* str;

  str = (char *) malloc(20 * sizeof(char));
  for (i = 0; i < residues; i++) {
    sprintf(str, "%d", i+1);
    if (isNameMatch(residueName[i], p) || isNameMatch(str, p))
      for (j = ipres[i]; j < ipres[i+1]; j++)
        mask[j-1] = 'T';
  }
  free(str);

}

void all_select(char *mask, int atoms) {
  int j;
  
  for (j = 0; j < atoms; j++)
    mask[j] = 'T';
}

void atnum_select(int at1, int at2, char *mask, int atoms) {
  int j;

  for (j = 0; j < atoms; j++)
    if (j+1 >= at1 && j+1 <= at2) 
      mask[j] = 'T';
}

void 
atname_select(char *p, char *mask, int atoms, Name *atomName) 
{
  int j;
  char* str;

  str = (char *) malloc(20 * sizeof(char));
  for (j = 0; j < atoms; j++) {
    sprintf(str, "%d", j+1);
    if (isNameMatch(atomName[j], p)|| isNameMatch(str, p))
      mask[j] = 'T';
  }
  free(str);
}

/*DAN DEBUG : Disabled */
/*void attype_select(char *p, char *mask, int atoms, Name *atomName) {
  int j;
  Atom *atom;
  
  for (j = 0; j < atoms; j++) {
    atom = ((parm->atom)+j);
    if (isNameMatch(atom->isymbl, p))
      mask[j] = 'T';
  }
}*/

void atelem_select(char *p, char *mask, int atoms, Name *atomName) {
  int j,isMatch;
  
  for (j = 0; j < atoms; j++) {
    isMatch=isElemMatch(atomName[j], p);
    if (isMatch==1)
      mask[j] = 'T';
    else if (isMatch==-1)
      return;
  }
}

int isElemMatch(char *s1, char *s2) {
  /* atom element type in AMBER starts at the first(!) position and may be 
   * at most two characters long (Ca,Mg,Fe), most are just 1 char long (C,O,H) 
   * this would be different for PDB file, where atom type should be at the
   * second position out of 4 chars for atom names */

  int typelen;

  typelen = strlen(s2);
  
  switch (typelen) {
  case 0:
    fprintf(stderr,"atom type not specified?\n");
    return(-1);
  case 2:
    if ( s1[1] != s2[1] ) return(0); /* false */
    if ( s1[0] != s2[0] ) return(0); /* false */
  case 1:
    if ( s1[0] != s2[0] ) return(0); /* false */
    return(1);            /* else return true */
  } 

  /* typelen > 2 */
  fprintf(stderr,"atom type (=element) shouldn't be longer than 2 chars\n");
  return(-1);

} /* isElemMatch */

int isNameMatch(char *s1, char *s2) {
  /* s1 is from prmtop (may have spaces), 
     s2 is from mask (doesn't have spaces) */
  int i;
  char *p;
  
  i = 0;
  for (p = s1; *p; p++) { 
    if (s2[i] == '*') {
      	for (; *p; p++) {
        	if (isNameMatch(p,&s2[i+1]))
            	return(1);
        }
        return (isNameMatch(p,&s2[i+1]));
      }
    else if (s2[i] == '?')
      i++;
    else if (*p == ' ')  /* omit ' ' as in ' CA \0', maybe problem for other String Match.   */
      continue;           
    else if (*p != s2[i++])    
      return(0); /* false */
  }
  if (s2[i] == '*') {
    return (isNameMatch(p,&s2[i+1]));
  }
  else if (s2[i] == '\0') /* if both are '\0'. */
  	return(1); /* true */
  else
  	return(0);
} /* isNameMatch */

void 
residue_numlist(char *pp, char *mask, int residues, int *ipres) 
{
  char buffer[MAXSELE];
  char *p;
  int i = 0;
  int res1 = 0, res2 = 0;
  int dash = 0;

  for (p = pp ; *p != '\0'; p++) {
    if ( isdigit(*p) )
      buffer[i++] = *p;
    if ( *p == ',' || *(p+1) == '\0') {
      buffer[i] = '\0';
      if (dash == 0) {
        if ( sscanf(buffer, "%d", &res1) != 1) {
          fprintf(stderr,"Error: parsing residue mask\n");
          return;
        }
        resnum_select(res1, res1, mask, residues, ipres);
      } else {
        if ( sscanf(buffer, "%d", &res2) != 1) {
          fprintf(stderr,"Error: parsing residue mask\n");
          return;
        }
        resnum_select(res1, res2, mask, residues, ipres);
        dash = 0;
      }
      i = 0;
    } else if ( *p == '-' ) {
      buffer[i] = '\0';
      if ( sscanf(buffer, "%d", &res1) != 1) {
        fprintf(stderr,"Error: parsing residue mask\n");
        return;
      }
      dash = 1;
      i = 0;
    } 
    if ( !( isdigit(*p) || *p == ',' || *p == '-' ) ) {
      fprintf(stderr,"Error: unknown symbol ==%c== in residue number parsing.\n", *p);
      return;
    }
  }
} /* residue_numlist */

void 
residue_namelist(char *pp, char *mask, int residues, Name *residueName, int *ipres) 
{
  char buffer[MAXSELE];
  char *p;
  int i = 0;

  for (p = pp; *p != '\0'; p++) {
    if ( isalnum(*p) || *p == '*' || *p == '?' || *p == '+' )
      buffer[i++] = *p;
    if ( *p == '-' )  /* '-' is used in numeric context, */
      buffer[i++] = *p;
    if ( *p == ',' || *(p+1) == '\0') {
      buffer[i] = '\0';
      if (strchr(buffer, '-') && isdigit(buffer[0])) { /* '-' is used in numeric context, */
        residue_numlist(buffer, mask, residues, ipres);
      } else {
        resname_select(buffer, mask, residues, residueName, ipres);
      }
      i = 0;
    } 
    else if ( !( isalnum(*p) || *p == ',' || *p == '*' || *p == '?' || *p == '+' || *p == '-' ) ) {
      fprintf(stderr,"Error: unknown symbol ==%c== in residue name parsing.\n", *p);
      return;
    }
  }
} /* residue_namelist */


void 
atom_numlist(char *pp, char *mask, int atoms) 
{
  char buffer[MAXSELE];
  char *p;
  int i = 0;
  int at1 = 0, at2 = 0;
  int dash = 0;

  /* put more error checks into this routine ? */
  
  for (p = pp; *p != '\0'; p++) {
    if ( isdigit(*p) )
      buffer[i++] = *p;
    if ( *p == ',' || *(p+1) == '\0') {
      buffer[i] = '\0';
      if (dash == 0) {
        if ( sscanf(buffer, "%d", &at1) != 1) {
          fprintf(stdout,"Error: parsing atom mask\n");
          return;
        }
        atnum_select(at1, at1, mask, atoms);
      } else {
        if ( sscanf(buffer, "%d", &at2) != 1) {
          fprintf(stdout,"Error: parsing atom mask\n");
          return;
        }
        atnum_select(at1, at2, mask, atoms);
        dash = 0;
      }
      i = 0;
    } else if ( *p == '-' ) {
      buffer[i] = '\0';
      if ( sscanf(buffer, "%d", &at1) != 1) {
        fprintf(stdout,"Error: parsing atom mask\n");
        return;
      }
      dash = 1;
      i = 0;
    } 
    if ( !( isdigit(*p) || *p == ',' || *p == '-' ) ) {
      fprintf(stderr,"Error: unknown symbol ==%c== in atom number parsing.\n", *p);
      return;
    }
  }
} /* atom_numlist */

void 
atom_namelist(char *pp, char *mask, int atoms, Name *atomName) 
{
  char buffer[MAXSELE];
  char *p;
  int i = 0;

  for (p = pp; *p != '\0'; p++) {
    if ( isalnum(*p) || *p == '*' || *p == '?' || *p == '+' || *p == '\'') 
      buffer[i++] = *p;
    if ( *p == '-') 
      buffer[i++] = *p;
    if ( *p == ',' || *(p+1) == '\0') {
      buffer[i] = '\0';
      if (strchr(buffer, '-') && isdigit(buffer[0])) {  /* '-' is used in numeric context, */
      	atom_numlist(buffer, mask, atoms);
      } else {
      	atname_select(buffer, mask, atoms, atomName);
      }
      i = 0;
      continue;
    } 
    if ( !( isalnum(*p) || *p == ',' || *p == '?' || *p == '*' || *p == '\'' || *p == '+' || *p == '-') ) {
      fprintf(stderr,"Error: unknown symbol ==%c== in atom name parsing.\n", *p);
      return;
    }
  }
} /* atom_namelist */

void 
atom_typelist(char *pp, char *mask, int atoms, Name *atomName) 
{
  char buffer[MAXSELE];
  char *p;
  int i = 0;

  for (p = pp; *p != '\0'; p++) {
    if ( isalnum(*p) || *p == '*' || *p == '?' || *p == '\'') 
      buffer[i++] = *p;
    if ( *p == ',' || *(p+1) == '\0') {
      buffer[i] = '\0';
      /* DAN DEBUG: DISABLED 
      attype_select(buffer, mask, atoms, atomName);*/
      fprintf(stderr,"WARNING: Atom type selection currently disabled!\n");
      i = 0;
    } 
    if ( !( isalnum(*p) || *p == ',' || *p == '?' || *p == '*' || *p == '\'') ) {
      fprintf(stderr,"Error: unknown symbol ==%c== in atom type parsing.\n", *p);
      return;
    }
  }
} /* atom_typelist */

void 
atom_elemlist(char *pp, char *mask, int atoms, Name *atomName) 
{
  char buffer[MAXSELE];
  char *p;
  int i = 0;

  for (p = pp; *p != '\0'; p++) {
    if ( isalpha(*p)) 
      buffer[i++] = *p;
    if ( *p == ',' || *(p+1) == '\0') {
      buffer[i] = '\0';
      atelem_select(buffer, mask, atoms, atomName);
      i = 0;
    } 
    if ( !( isalpha(*p) || *p == ',') ) {
      fprintf(stderr,"Error: unknown symbol ==%c== in atoms element parsing.\n", *p);
      return;
    }
  }
} /* atom_elemlist */

char * 
selectElemMask(char * elmaskstr, int atoms, int residues, Name *atomName,
               Name *residueName, int *ipres) 
{
  int i;
  int atomlist, reslist;  /* change that to enum type?? */
  char *pElemMask, *p;
  int buffer_p;
  char buffer[MAXSELE];
  
  pElemMask = (char *) malloc( atoms * sizeof(char));
  for (i = 0; i < atoms; i++)
    pElemMask[i] = 'F';

  if ( *elmaskstr == ':' ) { /* residue mask expression */
    buffer_p = 0;
    buffer[0] = '\0';
    reslist = NUMLIST;
    for (p = elmaskstr+1; *p != '\0'; p++){
      buffer[buffer_p++] = *p;
      if ( *p == '*' ) {
        if ( buffer_p == 0 && (*(p+1) == ',' || *(p+1) == '\0'))
          reslist = ALL;
        else if (reslist == NUMLIST) {
          reslist = NAMELIST;
        }
      }
      else if (isalpha(*p) || *p == '?'){
        reslist = NAMELIST;
      } 
/*      if ( *p == ',' || *(p+1) == '\0') {*/
      if (*(p+1) == '\0') {
        buffer[buffer_p] = '\0';
        buffer_p = 0;
      }
      if (buffer[0] != '\0' && buffer_p == 0) {
        switch (reslist) {
          case ALL:
            all_select(pElemMask, atoms);
            break;
          case NUMLIST:
            residue_numlist(buffer, pElemMask, residues, ipres);
            break;
          case NAMELIST: 
            residue_namelist(buffer, pElemMask, residues, residueName, ipres);
        }
/*        if (reslist == NAMELIST) */
          reslist = NUMLIST;
      }
    }
  } else if ( *elmaskstr == '@' ) {   /* atom selection mask */
    /* because atom names can have digits, and even can start with
       a digit, we need to search the whole expression to decide
       whether it's an atom numlist or namelist and it's still ambiguous.
       
       It should be OK now, since anything with non-numerical will be treated
       as NAMELIST, and the residue or atom number will be searched in the NAME search. */
    buffer_p = 0;
    buffer[0] = '\0';
    atomlist = NUMLIST;
    for (p = elmaskstr+1; *p != '\0'; p++){
      buffer[buffer_p++] = *p;
      if ( *p == '*' ) {
        if ( buffer_p == 0 && (*(p+1) == ',' || *(p+1) == '\0'))
          atomlist = ALL;
        else if (atomlist == NUMLIST) {
          atomlist = NAMELIST;
        }
      }
      else if (isalpha(*p) || *p == '?'){
        if(atomlist == NUMLIST)
          atomlist = NAMELIST;
      } 
      else if ( *p == '%' ) {
        atomlist = TYPELIST;
      } 
      else if ( *p == '/' ) {
        atomlist = ELEMLIST;
      } /* endif */
/*      if ( *p == ',' || *(p+1) == '\0') {*/
      if ( *(p+1) == '\0') {
        buffer[buffer_p] = '\0';
        buffer_p = 0;
      }
      
      if (buffer[0] != '\0' && buffer_p == 0) {
        switch (atomlist) {
          case ALL:
            all_select(pElemMask, atoms);
            break;
          case NUMLIST:
            atom_numlist(buffer, pElemMask, atoms);
            break;
          case NAMELIST:
            atom_namelist(buffer, pElemMask, atoms, atomName);
            break;
          case TYPELIST:
            /* DAN DEBUG: Currently disabled, this needs access to the parm structure */
            /*atom_typelist(buffer+1, pElemMask, atoms, atomName);*/
            fprintf(stderr,"ERROR: TYPELIST DISABLED!\n");
            break;
          case ELEMLIST:    /* because there's '/' after '@', position is +2 */
            atom_elemlist(buffer+1, pElemMask, atoms, atomName);
        } /* end switch */
/*        if (atomlist == NAMELIST) */
          atomlist = NUMLIST;
      }
    }
  } else if ( *elmaskstr == '*' ) {
    /* this is here just for compatibility with ptraj's notion of
     * selecting all residues by '*' as opposed to ":*" */
    all_select(pElemMask, atoms);
  } else if ( strchr("<>", *elmaskstr) ) {
    free(pElemMask);
    pElemMask = (char *) malloc( strlen(elmaskstr) * sizeof(char));
    strcpy(pElemMask, elmaskstr);
  } else {
    fprintf(stderr,"Error: elementary mask ==%s== contains nor : neither @\n",elmaskstr);
    free(pElemMask);
    return NULL;
  }
  
  return(pElemMask);
  
} /* selectElemMask */


char * 
parseMaskString(char *maskstr, int atoms, int residues, Name *atomName,
                       Name *residueName, int *ipres, void *X, char type,
                int debug) 
{
  /* this routine is called from ptraj.c:processAtomMask() which
   * provides access to atoms, residues, atomName[], residueName[]
   * and ipres[]. 
   * It returns a character mask array mask[i]='T'|'F', i=0,atoms-1
   * which contains the resulting atom selection
   */

  prnlev=debug;
  if (prnlev>0) fprintf(stderr,"In parseMaskString, debug active!\n");

  char infix[MAXSELE], postfix[MAXSELE];
  char *mask;

  if (prnlev > 5) 
    printf("original : ==%s==\n", maskstr);

  /* 1) preprocess input expression */
  if (tokenize(maskstr, infix)!=0) return NULL;
  if (prnlev > 5)
    printf("tokenized: ==%s==\n", infix);

  /* 2) construct postfix (RPN) notation */
  if (torpn(infix, postfix)!=0) return NULL;
  if (prnlev > 5)
    printf("postfix  : ==%s==\n", postfix);

  /* 3) evaluate postfix notation */
  mask = eval(postfix, atoms, residues, atomName, residueName, ipres, X, type);

  return(mask);
  
} /* parseMaskString */

