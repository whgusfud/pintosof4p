/*[X]this file defines how to do the calculation between float numbers*/

/*We use the 17.14 form of floating point*/
#define FLOATM 16384

/*convert n to float*/
#define INT2FLO(n) (n*FLOATM)

/*convert x to int (rounding toward zero)*/
#define F2ITZERO(x) (x/FLOATM)

/*Convert x to integer (rounding to nearest) */
#define F2ITNEAR(x) (x>=0?((x+FLOATM/2)/FLOATM):((x-FLOATM/2)/FLOATM))

/*Add float x and int n*/
#define FADDI(x,n) (x+n*FLOATM)

/*Subtract int n from float x */
#define FSUBI(x,n) (x-n*FLOATM) 

/*Multiply float x by int y:*/
#define FMUL(x,y) (((int64_t)x)*y/FLOATM)

/*Multiply float x by int n:*/
#define FMULI(x,n) (x*n)

/*Divide float x by int y:*/
#define FDIV(x,y) (((int64_t)x)*FLOATM/y)

/*Divide x by n:*/
#define FDIVI(x,n) (x/n)





 





