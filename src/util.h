#ifndef _UTIL_H__
#define _UTIL_H__

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "pqueue.h"

#define PI 3.14159265

// queue node
typedef struct QNode {
   void* ptr;
   struct QNode* next;
} QNode;

// pair
typedef struct Pair {
   int var;
   int val;
} Pair;

int g_int_compare(const void* a, const void* b);

int g_int_compare2(const void* a, const void* b, void* c);

int g_double_compare_high_null(const void* a, const void* b, void* c);

int get_first(void* key, void* value, void* data);

// Log Utility Functions

double logc(double x);

double entropy(double x);

double expect(double a, double b);

///////////////////// End Log Utility Functions


// Solver Functions

typedef struct pollog {
   double v;
   int p;
} pollog;

pollog* create_pollog(double v);

pollog* create_pollog_fromlog(double v);

double pl_convert(pollog* pl);

pollog* pl_copy(pollog* pl);

pollog* pl_loglog(pollog* pl);

pollog* pl_logprod(pollog* pl1, pollog* pl2);

pollog* pl_logdiv(pollog* pl1, pollog* pl2);

pollog* pl_logexp(pollog* pl1, int pow);

pollog* pl_logsum(pollog* pl1, pollog* pl2);

double* solve_cubic(double a, double b, double c, double d);

double* solve_cubic_old(double a, double b, double c, double d);

double newton_mix_comp_solver(int numx, pollog** a, double outera, pollog** q2, pollog** b, pollog** p);

double newton_double_solver(double a, double b, double c, double d);

// y = a*ln(x) + b*ln(1-x) +  c*x + d
double newton_double_log_solver(double a, double b, double c, double d);

double* get_quartic_roots(double A, double B, double C, double D, double E);

//0 = a*ln x + bx + c
double newton_compute_x(double a, double b, double c, double x0);

//////////// End Solver Functions

// Log Space Functions

double logsum_float(float x, float y);

double logsumarr_float(float* x, int num);

double logsum(double x, double y);

double logsumarr(double* x, int num);

double* logsumarr_pol(double* x, int* pol, int num);

/////////// End Log Space Functions

// Sum and Max SPN Functions

float spn_logsum(float* arr, int num, int* idx);

float spn_max(float* arr, int num, int* idx);

////////// End Sum and Max SPN Functions

// Arrays Accessor
typedef struct ArraysAccessor {
   void *** arrs_;
   int narrs_;
   int* arrlen_;
   int* currIdx_;
   void** curr_;
   int reset_;
   int noComb_;   
} ArraysAccessor;


// Takes control of arrs and arrlen
ArraysAccessor* createArraysAccessor(void*** arrs, int narrs, int* arrlen);
int hasNextCombinationArraysAccessor(ArraysAccessor* aa);
void** nextArraysAccessor(ArraysAccessor* aa);
void resetArraysAccessor(ArraysAccessor* aa);
void freeArraysAccessor(ArraysAccessor* aa);
int numCombinationsInArraysAccessor(const ArraysAccessor* aa);

// Int Accessor
typedef struct IntAccessor {
   int* max_;
   int narrs_;
   int* curr_;
   int reset_;
   int noComb_;   
} IntAccessor;


// Takes control of arrs and arrlen
IntAccessor* createIntAccessor(int* max_, int narrs);
int* nextIntAccessor(IntAccessor* aa);
void resetIntAccessor(IntAccessor* aa);
void freeIntAccessor(IntAccessor* aa);
int numCombinationsInIntAccessor(const IntAccessor* aa);

#endif
