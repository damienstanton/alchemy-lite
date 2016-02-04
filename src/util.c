#include "util.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define PI 3.14159265

int g_int_compare(const void* a, const void* b) {
   const int* ai = (int*)a;
   const int* bi = (int*)b;
   return *ai - *bi;
}

int g_int_compare2(const void* a, const void* b, void* c) {
   const int* ai = (int*)a;
   const int* bi = (int*)b;
   return *ai - *bi;
}

int g_double_compare_high_null(const void* a, const void* b, void* c) {
   const double* ai = (double*)a;
   const double* bi = (double*)b;
   double d = *bi - *ai;
   if (d < 0) return -1;
   else if (d > 0) return 1;
   else return 0;
}

int get_first(void* key, void* value, void* data) {
   *(double*)data = *(double*)key;
   return 1;
}

// Log Utility Functions

double logc(double x) {
   double logx = log(x);
   return logx;
}

double entropy(double x) {
   double logx = log(x);
//   if (logx == 0 && x == 0) return 0;
   if (isnan(x*logx)) return 0;
   return x*log(x);
}

double expect(double a, double b) {
   double logb = log(b);
//   if (logb==0 && a == 0) return 0;
   if (isnan(a*logb)) return 0;
   return a*logb;
}

///////////////////// End Log Utility Functions


pollog* create_pollog(double v) {
   pollog* pl = malloc(sizeof(pollog));
   if (v < 0) {
      pl->v = log(-1*v);
      pl->p = 0;
   } else {
      pl->v = log(v);
      pl->p = 1;
   }
   return pl;
}

pollog* create_pollog_fromlog(double v) {
   pollog* pl = malloc(sizeof(pollog));
   pl->p = 1;
   pl->v = v;
   return pl;
}

double pl_convert(pollog* pl) {
   if (pl->p == 0) {
      return -1*exp(pl->v);
   } else {
      return exp(pl->v);
   }
}

pollog* pl_copy(pollog* pl) {
   pollog* newpl = malloc(sizeof(pollog));
   newpl->v = pl->v;
   newpl->p = pl->p;
   return newpl;
}

pollog* pl_loglog(pollog* pl) {
   pollog* newpl = malloc(sizeof(pollog));
   if (pl->p == 0) {
      printf("loglog error.\n");
      exit(1);
   } else {
      if (pl->v < 0) {
         newpl->p = 0;
         newpl->v = log(-1*pl->v);
      } else {
         newpl->p = 1;
         newpl->v = log(pl->v);
      }
   }
   return newpl;
}

pollog* pl_logprod(pollog* pl1, pollog* pl2) {
   pollog* pl = malloc(sizeof(pollog));
   pl->v = pl1->v + pl2->v;
   if (pl1->p == pl2->p)
      pl->p = 1;
   else
      pl->p = 0;
   return pl;
}

pollog* pl_logdiv(pollog* pl1, pollog* pl2) {
   pollog* pl = malloc(sizeof(pollog));
   pl->v = pl1->v - pl2->v;
   if (pl1->p == pl2->p)
      pl->p = 1;
   else
      pl->p = 0;
   return pl;
}

pollog* pl_logexp(pollog* pl1, int pow) {
   pollog* pl = malloc(sizeof(pollog));
   if (pow % 2 == 0) { // even power
      pl->v = pow*pl1->v;
      pl->p = 1; 
   } else { // odd power
      pl->v = pow*pl1->v;
      pl->p = pl1->p;
   }
   return pl; 
}

pollog* pl_logsum(pollog* pl1, pollog* pl2) {
   return create_pollog(pl_convert(pl1)+pl_convert(pl2));
   pollog* pl = malloc(sizeof(pollog));
   double diff;
   if (pl1->v < (pl2->v - log(1e200))) {
      pl->v = pl2->v;
      pl->p = pl2->p;
      return pl;
   }
   if (pl2->v < (pl1->v - log(1e200))) {
      pl->v = pl1->v;
      pl->p = pl2->p;
      return pl;
   }
   if (isinf(pl1->v) && isinf(pl2->v)) {
      pl->v = log(0);
      pl->p = 1;
      return pl;
   }
   if (pl1->v > pl2->v) {
      diff = pl2->v - pl1->v;
      if (!isinf(exp(diff))) {
         if ((pl1->p == 1 && pl2->p == 1) ||
             (pl1->p == 0 && pl2->p == 0))
            pl->v = exp(diff);
         else
            pl->v = -1*exp(diff);
      }
      pl->v++;
      if (pl->v < 0) {
         pl->v *= -1;
         if (pl1->p == 1) pl->p = 0;
         else pl->p = 1;
      } else {
         if (pl1->p == 1) pl->p = 1;
         else pl->p = 0;
      }
      pl->v = pl1->v + log(pl->v);
   } else {
      diff =  pl1->v - pl2->v;
      if (!isinf(exp(diff))) {
         if ((pl1->p == 1 && pl2->p == 1) ||
             (pl1->p == 0 && pl2->p == 0))
            pl->v = exp(diff);
         else
            pl->v = -1*exp(diff);
      }
      pl->v++;
      if (pl->v < 0) {
         pl->v *= -1;
         if (pl2->p == 1) pl->p = 0;
         else pl->p = 1;
      } else {
         if (pl2->p == 1) pl->p = 1;
         else pl->p = 0;
      }
      pl->v = pl2->v + log(pl->v);
     
   }
   return pl;
}

double* solve_cubic(double a, double b, double c, double d) {
   double* roots = calloc(3, sizeof(double));

   printf("y = %f*x^3 + %f*x^2 + %f*x + %f\n", a, b, c, d);

   pollog* pA = create_pollog(a);
   pollog* pB = create_pollog(b);
   pollog* pC = create_pollog(c);
   pollog* pD = create_pollog(d);


   pollog* delta2;
   pollog* h2;
   pollog* d2;
   pollog* d2h2;
   pollog* theta;
   pollog* pqsqr;

   pollog* p;
   pollog* q;

   pollog* temp1;
   pollog* temp2;
   pollog* temp3;
   pollog* temp4;

   pollog* b3a = pl_logdiv(pB,pA);
   b3a->v -= log(3.0);
   if (b3a->p == 0)
      b3a->p = 1;
   else
      b3a->p = 0;

   //p = (3ac - b^2) / 3a^2   
   temp1 = pl_logprod(pA,pC);
   temp1->v += log(3.0);
   temp2 = pl_logexp(pB,2);
   if (temp2->p == 0)
      temp2->p = 1;
   else
      temp2->p = 0;
   temp3 = pl_logsum(temp1, temp2);
   free(temp1);
   free(temp2);

   temp1 = pl_logexp(pA,2);
   temp1->v += log(3.0);
   p = pl_logdiv(temp3,temp1);
   free(temp3);
   free(temp1);

   // p = -3delta^2 => delta^2 = -p/3
   delta2 = pl_copy(p);
   delta2->v -= log(3.0);
   if (delta2->p == 0)
      delta2->p = 1;
   else
      delta2->p = 0;

   //q = (2b^3 - 9abc + 27a^2d)/27a^3
   temp1 = pl_logexp(pB,3);
   temp1->v += log(2.0);
   temp2 = pl_logprod(pA,pB);
   temp3 = pl_logprod(temp2, pC);
   free(temp2);
   temp3->v += log(9.0);
   if (temp3->p == 0)
      temp3->p = 1;
   else
      temp3->p = 0;
   temp2 = pl_logsum(temp1, temp3);
   free(temp1);
   free(temp3);
   temp3 = pl_logexp(pA,2);
   temp1 = pl_logprod(temp3, pD);
   temp1->v += log(27.0);
   free(temp3);
   temp3 = pl_logsum(temp2, temp1);
   free(temp2);
   free(temp1);

   temp1 = pl_logexp(pA,3);
   temp1->v += log(27.0);

   q = pl_logdiv(temp3,temp1);
   free(temp1);
   free(temp3);

   printf("0 = %f*x^3 + %f*x + %f\n", 1.0, pl_convert(p), pl_convert(q));
   
   // q = d/a  => d = qa
   temp1 = pl_logprod(q,pA);
   d2 = pl_logexp(temp1, 2);
   free(temp1);

   temp1 = pl_logexp(delta2,3);
   temp2 = pl_logexp(pA,2);
   h2 = pl_logprod(temp1,temp2);
   h2->v += log(4.0);
   free(temp1);
   free(temp2);
   free(delta2);

   temp1 = pl_copy(h2);
   if (temp1->p == 0)
      temp1->p = 1;
   else
      temp1->p = 0;
   d2h2 = pl_logsum(d2,temp1);
   free(temp1);
   free(d2);
   free(h2);   

   printf("d2h2 = %f (%d)\n", pl_convert(d2h2), d2h2->p);
   if (d2h2->p == 1) {
      temp1 = pl_logexp(q,2);
      temp1->v -= log(4.0);
      temp2 = pl_logexp(p,3);
      temp2->v -= log(27.0);
      pqsqr = pl_logsum(temp1, temp2);
      pqsqr->v *= 0.5;   
      printf("sqrt = %f\n", pl_convert(pqsqr));
      free(temp1);
      free(temp2);

      temp1 = pl_copy(q);
      temp1->v -= log(2.0);
      if (temp1->p == 0)
         temp1->p = 1;
      else
         temp1->p = 0;

      temp2 = pl_logsum(temp1, pqsqr);
      printf("first cbrt(%f) =", pl_convert(temp2));
      temp2->v *= (1.0/3.0);
      printf(" %f\n", pl_convert(temp2));

      if (pqsqr->p == 0)
         pqsqr->p = 1;
      else
         pqsqr->p = 0; 
      
      temp3 = pl_logsum(temp1, pqsqr);
      printf("second cbrt(%f) =", pl_convert(temp3));
      temp3->v *= (1.0/3.0);
      printf(" %f\n", pl_convert(temp3));
      free(temp1);
      temp1 = pl_logsum(temp2, temp3);
      free(temp2);
      free(temp3);
      free(pqsqr);
      printf("b3a = %f\n", pl_convert(b3a));
      temp3 = pl_logsum(temp1,b3a);
      roots[0] = pl_convert(temp3);
      roots[1] = log(-1);
      roots[2] = log(-1);

      free(temp1);
      free(temp3);
      free(d2h2);
   } else if (isinf(d2h2->v)) {
      printf("d2h2 == 0\n");
      exit(1);
   } else {
      delta2 = pl_copy(p);
      delta2->v-= log(3.0);
      if (delta2->p == 0)
         delta2->p = 1;
      else
         delta2->p = 0;

      h2 = pl_logexp(delta2,3);
      h2->v *= (1.0/3.0);
      h2->v += log(2.0);

      temp1 = pl_logdiv(q,h2);
      if (temp1->p == 0)
         temp1->p = 1;
      else
         temp1->p = 0;
      theta = create_pollog(acos(pl_convert(temp1)));
      theta->v += log(1.0/3.0);
      free(temp1);
     
      delta2->v *= 0.5;
      temp1 = create_pollog(cos(pl_convert(theta)));
      
      temp2 = pl_logprod(delta2,temp1);
      temp2->v += log(2.0);
      free(temp1);
     
      temp3 = pl_logsum(temp2,b3a);
      roots[0] = pl_convert(temp3);
      free(temp3);
      free(temp2);

      temp2 = create_pollog(cos(2.0*PI/3.0 - pl_convert(theta))); 
      temp1 = pl_logprod(delta2,temp2);
      temp1->v += log(2.0);
      free(temp2);
      temp2 = pl_logsum(temp1,b3a);
      roots[1] = pl_convert(temp2);
      free(temp2);
      free(temp1);
   
      temp2 = create_pollog(cos(2.0*PI/3.0 + pl_convert(theta)));
      temp1 = pl_logprod(delta2,temp2);
      temp1->v += log(2.0);
      free(temp2);
      temp2 = pl_logsum(temp1,b3a);
      roots[2] = pl_convert(temp2);
      free(temp2);
      free(temp1); 
   }

   return roots;
}

double* solve_cubic_old(double a, double b, double c, double d) {
   double* roots = calloc(3, sizeof(double));

   printf("y = %f*x^3 + %f*x^2 + %f*x + %f\n", a, b, c, d);

   pollog* pA = create_pollog(a);
   pollog* pB = create_pollog(b);
   pollog* pC = create_pollog(c);
   pollog* pD = create_pollog(d);
  
 
   pollog* delta2;
   pollog* h2;
   pollog* d2h2;
   pollog* theta;

   pollog* p;
   pollog* q;

   pollog* temp1;
   pollog* temp2;
   pollog* temp3;
   pollog* temp4;

   temp1 = pl_logexp(pB, 2);
   temp2 = pl_logprod(pA, pC);
   temp2->v += log(3.0);
   if (temp2->p == 0)
      temp2->p = 1;
   else
      temp2->p = 0;
   temp3 = pl_logsum(temp1, temp2);
   free(temp1);
   free(temp2);

   temp2 = pl_logexp(pA,2);
   temp2->v += log(9.0);
   delta2 = pl_logdiv(temp3, temp2);
   free(temp2);
   free(temp3);

   temp1 = pl_logexp(delta2, 3);
   temp2 = pl_logexp(pA,2);
   h2 = pl_logprod(temp2,temp1);
   h2->v += log(4.0);
   free(temp1);
   free(temp2);

   temp1 = pl_logexp(pD,2);
   temp2 = pl_copy(h2);
   if (temp2->p == 0)
      temp2->p = 1;
   else
      temp2->p = 0;
   d2h2 = pl_logsum(temp1, temp2);
   free(temp1);
   free(temp2);
   printf("d2h2 = %f\n", pl_convert(d2h2));
   if (d2h2->p == 0) {
      temp2 = pl_copy(h2);
      temp2->v *= 0.5;

      temp1 = pl_logdiv(pD,temp2);
      free(temp2);
      if (temp1->p == 0)
         temp1->p = 1;
      else
         temp1->p = 0;
      theta = create_pollog(acos(pl_convert(temp1)));
      free(temp1);
      theta->v += log(1.0/3.0);
     
      temp1 = create_pollog(cos(pl_convert(theta)));
      temp2 = pl_copy(delta2);
      temp2->v *= 0.5;
      temp3 = pl_logprod(temp2, temp1);
      temp3->v += log(2.0);
      free(temp1);
      free(temp2);
      roots[0] = pl_convert(temp3);
      free(temp3);

      temp1 = create_pollog(2.0*PI);
      temp1->v -= log(3.0); 
      temp2 = pl_copy(theta);
      temp4 = pl_logsum(temp1, temp2);
      if (temp2->p == 0)
         temp2->p = 1;
      else
         temp2->p = 0;
      temp3 = pl_logsum(temp1, temp2);
      free(temp1);
      free(temp2);
      temp1 = create_pollog(cos(pl_convert(temp3)));
      free(temp3);
      temp2 = pl_copy(delta2);
      temp2->v *= 0.5;
      temp3 = pl_logprod(temp2, temp1);
      temp3->v += log(2.0);
      free(temp1);
      roots[1] = pl_convert(temp3);
      free(temp3);

      temp1 = create_pollog(cos(pl_convert(temp4)));
      free(temp4);
      temp3 = pl_logprod(temp2, temp1);
      temp3->v += log(2.0);

      temp2 = pl_copy(pA);
      temp2->v += log(3.0);
      temp4 = pl_logdiv(pB,temp2);
      free(temp2);
      if (temp4->p == 0)
         temp4->p = 1;
      else
         temp4->p = 0;
      temp2 = pl_logsum(temp3,temp4);

      roots[2] = pl_convert(temp2);
      free(temp1);
      free(temp2);
      free(temp3);
   } else if (isinf(d2h2->v)) {
      printf("d2h2 == 0\n");
      exit(1);
   } else {
      temp1 = pl_logprod(pA,pC);
      temp1->v += log(3.0);
      temp2 = pl_logexp(pB,2);
      if (temp2->p == 0)
         temp2->p = 1;
      else
         temp2->p = 0;
      temp3 = pl_logsum(temp1, temp2);
      free(temp1);
      free(temp2);
      temp1 = pl_logexp(pA,2);
      temp1->v += log(3.0);
      p = pl_logdiv(temp3,temp1);
      free(temp1);
      free(temp3);      

      temp1 = pl_logexp(pB,3);
      temp1->v += log(2.0);
      temp2 = pl_logprod(pA,pB);
      temp3 = pl_logprod(temp2,pC);
      temp3->v += log(9.0);
      if (temp3->p == 0)
         temp3->p = 1;
      else
         temp3->p = 0;
      free(temp2);
      temp2 = pl_logexp(pA,2);
      temp4 = pl_logprod(temp2,pD);
      free(temp2);
      temp4->v += log(27.0);
      temp2 = pl_logsum(temp1, temp3);
      free(temp1);
      free(temp3);
      temp1 = pl_logsum(temp2, temp4);
      free(temp2);
      free(temp4);
      temp2 = pl_logexp(pA,3);
      temp2->v += log(27.0);
      q = pl_logdiv(temp1, temp2);
      free(temp2);
      free(temp1);

      temp1 = pl_logexp(q,2);
      temp1->v -= log(4.0);
      temp2 = pl_logexp(p,3);
      temp2->v -= log(27.0);
      temp3 = pl_logsum(temp1, temp2);
      free(temp1);
      free(temp2);
      temp3->v *= 0.5; 

      temp1 = pl_copy(q);
      temp1->v -= log(2.0);
      if (temp1->p == 0)
         temp1->p = 1;
      else
         temp1->p = 0;

      temp2 = pl_logsum(temp1, temp3);
      temp2->v *= (1.0/3.0);
   
      if (temp3->p == 0)
         temp3->p = 1;
      else
      temp3->p = 0;
      temp4 = pl_logsum(temp1, temp3);
      temp4->v *= (1.0/3.0);
      free(temp1);
      free(temp3);
      
      temp1 = pl_logsum(temp2, temp4);
      free(temp2);
      free(temp4);
      temp2 = pl_copy(pA);
      temp2->v += log(3.0);
      temp4 = pl_logdiv(pB,temp2);
      free(temp2);
      if (temp4->p == 0)
         temp4->p = 1;
      else
         temp4->p = 0;
      temp2 = pl_logsum(temp1,temp4);
      free(temp1);
      free(temp4);
      roots[0] = pl_convert(temp2);
      roots[1] = sqrt(-1);
      roots[2] = sqrt(-1);
   }
   printf("roots: %f %f %f\n", roots[0], roots[1], roots[2]);
   return roots;
}

double newton_mix_comp_solver(int numx, pollog** a, double outera, pollog** q2, pollog** b, pollog** p) {
   double alpha;
   double newalpha;
   int iter = 0;
   pollog* numer;
   pollog* denom;
   pollog* inlog;
   int i;

   pollog* temp1;
   pollog* temp2;
   pollog* temp3;

   newalpha = 0.75;
   do { 
      alpha = newalpha;

      numer = create_pollog(0.0);
      denom = create_pollog(0.0);
      for (i = 0; i < numx; i++) {
         //numer += a[i]*p[i];
         temp1 = pl_logprod(a[i], p[i]);
         temp2 = numer;
         numer = pl_logsum(temp1, temp2);
         free(temp1);
         free(temp2);

         //numer -= a[i]*log((a[i]*alpha*outera)+b[i]+1);
         temp1 = pl_copy(a[i]);
         temp1->v += log(alpha);
         temp1->v += log(outera);
         temp2 = pl_logsum(temp1, b[i]);
         free(temp1);
//         temp1 = create_pollog(1.0);
         inlog = pl_logsum(temp2, q2[i]);
         free(temp2);
//         free(temp1);
         temp1 = pl_loglog(inlog);
         temp2 = pl_logprod(a[i],temp1);
         free(temp1);
         if (temp2->p == 0)
            temp2->p = 1;
         else
            temp2->p = 0;
         temp1 = numer;
         numer = pl_logsum(temp1, temp2);
         free(temp1);
         free(temp2);

         //denom += pow(a[i],2) / ((a[i]*alpha*outera)+b[i]+1);
         temp1 = pl_logexp(a[i],2);
         temp2 = pl_logdiv(temp1, inlog);
         free(temp1);
         temp1 = denom;
         denom = pl_logsum(temp1, temp2);
         free(temp1);
         free(temp2); 
      }
      //newalpha = -1*(numer / denom);
      // did not include -1 since we will be doing x - f(x)/f'(x)
      temp1 = pl_logdiv(numer,denom);
      temp2 = create_pollog(alpha);
      temp3 = pl_logsum(temp2, temp1);
      free(temp1);
      free(temp2);
      newalpha = pl_convert(temp3);
      free(temp3);

//      printf("new alpha=%f\n", newalpha);
      if (newalpha <= 0) {
         newalpha = alpha/2.0;
      } else if (newalpha > 1) {
         newalpha = alpha/2.0;
      }
      
//      printf("2new alpha=%f\n", newalpha);
      //if (newalpha <= 0.000001) exit(1);
      iter++;
   } while (iter < 100 && fabs(newalpha-alpha) > 0.0000001);
    
//   printf("x=%f\n", newalpha);
//   printf("Newton's took %d iterations.\n", iter);
   return newalpha;
}

double newton_double_solver(double a, double b, double c, double d) {
   double x;
   double newx;
   int iter = 0;   

   newx = 0.5;
   printf("a=%f, b=%f, c=%f, d=%f\n", a,b,c,d);
   do {
      x = newx;
      newx = x - (a*log(x)+b*log(1-x)+c*x+d)/((a/x) - (b/(1-x))+c);
      printf("newx=%f\n", newx);
      if (newx <= 0) {
         newx = x/2.0;
      } else if (newx > 1) {
         newx = x/2.0;
      }
      
      printf("2newx=%f\n", newx);
      if (newx <= 0.000001) exit(1);
      iter++;
   } while (iter < 100 && fabs(newx-x) > 0.0000001);
   printf("x=%f\n", newx);
   printf("Newton's took %d iterations.\n", iter);
   return newx;
}

// y = a*ln(x) + b*ln(1-x) +  c*x + d
double newton_double_log_solver(double a, double b, double c, double d) {

   pollog* pA = create_pollog(a);
   pollog* pB = create_pollog(b);
   pollog* pC = create_pollog(c);
   pollog* pD = create_pollog(d);

   int iter = 0;

   pollog* newx = create_pollog(0.05);
   pollog* x;

   pollog* temp1;
   pollog* temp2;
   pollog* temp3;
   pollog* temp4;
   pollog* temptop;

   pollog* one = create_pollog(1.0);

   double ret;
   printf("%f %f %f %f\n", pl_convert(pA), pl_convert(pB), pl_convert(pC), pl_convert(pD));
   do {
      x = pl_copy(newx);
      free(newx);

      temp1 = pl_loglog(x);
      temp2 = pl_logprod(pA,temp1);
      free(temp1);
      temp1 = pl_copy(x);
      if (temp1->p == 0)
         temp1->p = 1;
      else
         temp1->p = 0;
      temp4 = pl_logsum(one,temp1);
      free(temp1);
      temp1 = pl_loglog(temp4);
      free(temp4);
      temp3 = pl_logprod(pB,temp1);
      free(temp1);

      temp1 = pl_logsum(pC,x);
      temp4 = pl_logsum(temp1, pD);
      free(temp1);
      temp1 = pl_logsum(temp3, temp4);
      free(temp4);
      free(temp3);
      temptop = pl_logsum(temp2, temp1);
      free(temp2);
      free(temp1);
   // y = a*ln(x) + b*ln(1-x) +  c*x + d

      temp1 = pl_logdiv(pA,x);
      temp2 = pl_copy(x);
      if (temp2->p == 0)
         temp2->p = 1;
      else
         temp2->p = 0;
      temp3 = pl_logsum(one, temp2);
      free(temp2);
      temp2 = pl_logdiv(pB,temp3);
      if (temp2->p == 0)
         temp2->p = 1;
      else
         temp2->p = 0; 
      temp3 = pl_logsum(temp1, temp2);
      free(temp1);
      free(temp2);
     // temp1 = pl_logprod(pC,x);
      temp2 = pl_logsum(temp3, pC);
      //free(temp1);
      free(temp3);
      temp1 = temp2;
//      temp1 = pl_logsum(temp2, pD); 
//      free(temp2);

      temp2 = pl_logdiv(temptop, temp1);
      if (temp2-> p == 0)
         temp2->p = 1;
      else
         temp2->p = 0;
      newx = pl_logsum(x, temp2);
      free(temp2);

      if (newx->p == 0) {
         newx->v = x->v - log(2.0);
         newx->p = 1;
      }
      printf("newx:%f\n", pl_convert(newx));
      iter++;
   } while (iter < 100 && fabs(newx->v - x->v) > 0.0001);
   printf("Newton's took %d iterations.\n", iter);
   ret = pl_convert(newx);
   free(newx);
   free(x);
   return ret;
}

double* get_quartic_roots(double A, double B, double C, double D, double E) {
   double* roots = malloc(sizeof(double)*4);
   
   //Step 1
   double alpha;
   double beta;
   double gamma;

   //Step 2
   double P;
   double Q;
   double R;

   //Step 3
   double U;

   //Step 4
   double y;

   //Step 5
   double W;
   double first;
   double threea2y;
   double twobw;

   double delta2;
   double h2;
   double d2h2;
   double theta;

   printf("y=%f*x^4 + %f*x^3 + %f*x^2 +%f*x + %f\n", A, B, C, D, E);

   alpha = -((3*pow(B,2))/(8*pow(A,2))) + (C/A);
   beta = (pow(B,3)/(8*pow(A,3))) - ((B*C)/(2*pow(A,2))) + (D/A);
   gamma = -((3*pow(B,4))/(256*pow(A,4)))+((C*pow(B,2))/(16*pow(A,3)))-((B*D)/(4*pow(A,2)))+(E/A);

   printf("alpha=%f, beta=%f, gamma=%f\n", alpha, beta, gamma);

   if (beta == 0) {
      printf("beta = 0\n");
      roots[0] = -(B/(4*A))+sqrt((-alpha+sqrt(pow(alpha,2)-(4*gamma)))/2);
      roots[1] = -(B/(4*A))+sqrt((-alpha-sqrt(pow(alpha,2)-(4*gamma)))/2);
      roots[2] = -(B/(4*A))-sqrt((-alpha+sqrt(pow(alpha,2)-(4*gamma)))/2);
      roots[3] = -(B/(4*A))-sqrt((-alpha-sqrt(pow(alpha,2)-(4*gamma)))/2);
      return roots;
   }

   P = -(pow(alpha,2)/12)-gamma;
   Q = -(pow(alpha,3)/108) + ((alpha*gamma)/3) - (pow(beta,2)/8);
   printf("Q = %f + %f + %f\n", -(pow(alpha,3)/108), ((alpha*gamma)/3), - (pow(beta,2)/8));

   printf("v^3 + %fv + %f = 0\n", P, Q);
   printf("log(P) = %f\n", log(P));
   delta2 = (-3*P)/9.0;
   h2 = 4.0*pow(delta2,3);
   printf("delta2^3 = %f %f\n", pow(delta2,3), (4.0*pow(delta2,3)));
   d2h2 = pow(Q,2)-h2;
   printf("d=%f, h^2=%f,  d^2-h^2=%f\n", log(Q), log(h2), log(d2h2)); 
   if (d2h2 < 0) {
      printf("d2h2<0\n");
      theta = (1.0/3.0)*acos(-Q/sqrt(h2));
      U =  2.0*sqrt(delta2)*cos(theta);
      y = U - (5.0/6.0)*alpha;
      printf("U=%f\n", U);
   } else if (d2h2 == 0) {
      printf("d2h2=0\n");
      U = sqrt(delta2);
      y = U - (5.0/6.0)*alpha;
   } else {
      printf("d2h2 > 0\n");
      // Note R could be -(Q/2) - sqrt(....)
      R = (-Q + sqrt(d2h2))/2.0;
      printf("%f %f\n", log(pow(Q,6)), log(pow(sqrt(d2h2),6)));
      printf("%f %f\n", R, ((-Q - sqrt(d2h2))/2.0)); 
      printf("logs  P=%f, Q=%f, R=%f\n", log(P), log(Q), log(R));

/*      if (R < 0) {
         printf("R is < 0. Need a better way to compute cube root of R.\n");
         free(roots);
         exit(1);
      }*/
      U = cbrt(R);
      printf("U=%f\n", U);
    //  U = (-2.0/3.0)*U - U;
      printf("U=%f\n", U, log(U));
      if (U != 0) {
         printf("U != 0\n");
         y = -((5.0*alpha)/6.0)+U-(P/(3.0*U));
      } else { // U == 0
         y = -((5.0/6)*alpha)+U-cbrt(Q);
      }
   }
   printf("y=%f\n", y);
   
   W = sqrt(alpha + (2.0*y));
   printf("W=%f\n", W);
   
   first = -(B/(4.0*A));
   threea2y = (3.0*alpha)+(2.0*y);
   twobw = (2.0*beta)/W;
   printf("first=%f, 3a2y=%f, 2bw=%f\n", first, threea2y, twobw);

   roots[0] = first + ((W + sqrt(-(threea2y + twobw)))/2.0); 
   roots[1] = first + ((W - sqrt(-(threea2y + twobw)))/2.0); 
   roots[2] = first + ((-W + sqrt(-(threea2y - twobw)))/2.0); 
   roots[3] = first + ((-W - sqrt(-(threea2y - twobw)))/2.0); 
   printf("%f %f %f %f\n", roots[0], roots[1], roots[2], roots[3]);

   printf("%f\n", (A*pow(roots[0],4)+(B*pow(roots[0],3))+(C*pow(roots[0],2))+(D*roots[0])+E));
   printf("%f\n", (A*pow(roots[1],4)+B*pow(roots[1],3)+C*pow(roots[1],2)+D*roots[1]+E));
   printf("%f\n", (A*pow(roots[2],4)+B*pow(roots[2],3)+C*pow(roots[2],2)+D*roots[2]+E));
   printf("%f\n", (A*pow(roots[3],4)+B*pow(roots[3],3)+C*pow(roots[3],2)+D*roots[3]+E));
   printf("%f\n", (A*pow(-14.3489,4)+B*pow(-14.3489,3)+C*pow(-14.3489,2)+D*-14.3489+E));
 
   return roots;
}

//0 = a*ln x + bx + c
double newton_compute_x(double a, double b, double c, double x0) {
   double x;
   double newx;
   int iter = 0;   

   x0 = 0.5;
   newx = x0;
   printf("a=%f, b=%f, c=%f, x0=%f\n", a,b,c,x0);
   do {
      x = newx;
      newx = x - (a*log(x)+(b*x)+c)/((a/x)+b);
      printf("newx=%f\n", newx);
      if (newx <= 0) {
         newx = x/2.0;
      }
      
      printf("2newx=%f\n", newx);
      if (newx <= 0.000001) exit(1);
      iter++;
   } while (iter < 100 && fabs(newx-x) > 0.0000001);
   printf("x=%f\n", newx);
   printf("Newton's took %d iterations.\n", iter);
   return newx;
}

double logsum_float(float x, float y) {
   float diff;
   float ret;
   if (x < (y - log(1e200)))
      return y;
   if (y < (x - log(1e200)))
      return x;
   
   diff = x - y;
   if (isinf(exp(diff)))
      return (x > y ? x : y);

   ret = (y + log(1 + exp(diff)));
   return ret;
}

double logsumarr_float(float* x, int num) {
   float diff;
   float sum;
   int maxIdx = -1;
   float max = x[0];
   int i;

   for (i = 0; i < num; i++) {
      if (isfinite(x[i])) {
         max = x[i];
         maxIdx = i;
         break;
      }
   }
   
   for (i = 0; i < num; i++) {
      if (isfinite(x[i]) && x[i] > max) {
         max = x[i];
         maxIdx = i;
      }
   }

   sum = 0.0;
   for (i = 0; i < num; i++) {
      if (!isfinite(x[i])) continue;
      if (i == maxIdx) sum++;
      else {
   //      if (x[i] < max - log(1e200)) continue;
         diff = x[i] - max;
         if (!isinf(exp(diff)))
            sum += exp(diff);
      }
   }
   return max + log(sum);
}

double logsum(double x, double y) {
   double diff;
   double ret;
   if (x < (y - log(1e200)))
      return y;
   if (y < (x - log(1e200)))
      return x;
   
   diff = x - y;
   if (isinf(exp(diff)))
      return (x > y ? x : y);

   ret = (y + log(1 + exp(diff)));
   return ret;
}

double logsumarr(double* x, int num) {
   double diff;
   double sum;
   int maxIdx = -1;
   double max = x[0];
   int i;

   for (i = 0; i < num; i++) {
      if (isfinite(x[i])) {
         max = x[i];
         maxIdx = i;
         break;
      }
   }
   
   for (i = 0; i < num; i++) {
      if (isfinite(x[i]) && x[i] > max) {
         max = x[i];
         maxIdx = i;
      }
   }

   sum = 0.0;
   for (i = 0; i < num; i++) {
      if (!isfinite(x[i])) continue;
      if (i == maxIdx) sum++;
      else {
   //      if (x[i] < max - log(1e200)) continue;
         diff = x[i] - max;
         if (!isinf(exp(diff)))
            sum += exp(diff);
      }
   }
   return max + log(sum);
}

double* logsumarr_pol(double* x, int* pol, int num) {
   double diff;
   double* ret = malloc(sizeof(double)*2);
   double sum;
   double maxIdx = 0;
   double max = x[0];
   int maxPol = pol[0];
   int i;
   
   for (i = 0; i < num; i++) {
      if (x[i] > max) {
         max = x[i];
         maxIdx = i;
         maxPol = pol[i];
      }
   }

   sum = 0.0;
   for (i = 0; i < num; i++) {
      if (i == maxIdx) sum++;
      else {
     //    if (x[i] < max - log(1e200)) {
     //       continue;
     //    }
         diff = x[i] - max;
         if (!isinf(exp(diff))) {
            if ((pol[i] == 1 && maxPol == 1) || 
                  (pol[i] == 0 && maxPol == 0))
               sum += exp(diff);
            else
               sum -= exp(diff);
         }
      }
   }
   if (sum < 0) {
      sum = -1*sum;
      if (maxPol == 1) maxPol = 0;
      else maxPol = 1;
   } 
   ret[0] = max + log(sum);
   if (maxPol == 1)
      ret[1] = 1;
   else
      ret[1] = 0;
   return ret;
}

float spn_logsum(float* arr, int num, int* idx) {
   float diff;
   float sum;
   int maxIdx = -1;
   float max = arr[0];
   int i;

   *idx = -1;
   for (i = 0; i < num; i++) {
      if (isfinite(arr[i])) {
         max = arr[i];
         maxIdx = i;
         break;
      }
   }
   
   for (i = 0; i < num; i++) {
      if (isfinite(arr[i]) && arr[i] > max) {
         max = arr[i];
         maxIdx = i;
      }
   }

   sum = 0.0;
   for (i = 0; i < num; i++) {
      if (!isfinite(arr[i])) continue;
      if (i == maxIdx) sum++;
      else {
         diff = arr[i] - max;
         if (!isinf(exp(diff)))
            sum += exp(diff);
      }
   }
   return max + log(sum);
}

float spn_max(float* arr, int num, int* idx) {
   float max = arr[0];
   int maxIdx;
   int i;
//   printf("spn max:  "); 
   for (i = 0; i < num; i++) {
//      printf("%f ", arr[i]);
      if (isfinite(arr[i])) {
         max = arr[i];
         maxIdx = i;
         break;
      }
   }
   
   for (i = 0; i < num; i++) {
      if (isfinite(arr[i]) && arr[i] > max) {
         max = arr[i];
         maxIdx = i;
      }
   }
//   printf(" = %d\n", maxIdx);
   *idx = maxIdx;
   return max;
}

void resetArraysAccessor(ArraysAccessor* aa) {
   int i;
   int* currIdx = aa->currIdx_;
   int* num = aa->arrlen_;

   aa->reset_ = 1;
   for (i = 0; i < aa->narrs_; i++) {
      if (*num <= 0) {
         aa->noComb_ = 1;
         return;
      }
      else {
         *currIdx = 0;
      }
      num++;
      currIdx++;
   }
}

// Takes control of all input arrays
ArraysAccessor* createArraysAccessor(void*** arrs, int narrs, int* arrlen) {
   ArraysAccessor* aa = (ArraysAccessor*)malloc(sizeof(ArraysAccessor));

   aa->noComb_ = 0;
   aa->arrs_ = arrs;
   aa->narrs_ = narrs;
   aa->arrlen_ = arrlen;
   aa->curr_ = (void**)malloc(sizeof(void*)*narrs);
   aa->currIdx_ = (int*)malloc(sizeof(int)*narrs);
   resetArraysAccessor(aa);
   return aa;
}


void freeArraysAccessor(ArraysAccessor* aa) {
   int i, j;
   for (i = 0; i < aa->narrs_; i++) {
      free(aa->arrs_[i]);
   }
   free(aa->arrs_);
   free(aa->arrlen_);
   free(aa->curr_);
   free(aa->currIdx_);
}

int hasNextCombinationArraysAccessor(ArraysAccessor* aa) {
   if (aa->noComb_ == 1) return 0;
   if (aa->currIdx_[0] < 0) return 0;
   return 1;
}

void** nextArraysAccessor(ArraysAccessor* aa) {
   int i;
   int null = 0;
   if (aa->noComb_ == 1) return NULL;
   if (aa->reset_ == 1) {
      aa->reset_ = 0;
   }
   if (aa->currIdx_[0] < 0) return NULL;

   for (i = 0; i < aa->narrs_; i++) {
      aa->curr_[i] = aa->arrs_[i][aa->currIdx_[i]];
      if (aa->curr_[i] == NULL) null = 1;
   }
   for (i = aa->narrs_-1; i >= 0; i--) {
      aa->currIdx_[i]++;
      if (aa->currIdx_[i] < aa->arrlen_[i]) {
         if (null == 1) return NULL; else return aa->curr_;
      }
      aa->currIdx_[i] = 0;
   }
   aa->currIdx_[0] = -1;
   if (null == 1) return NULL;
   return aa->curr_;
}

int numCombinationsInArraysAccessor(const ArraysAccessor* aa) {
   int n = 1;
   int i;
   if (aa->narrs_ == 0) return 0;
   for (i = 0; i < aa->narrs_; i++) {
      if (aa->arrlen_[i] < 0) n = 0;
      else n *= aa->arrlen_[i];
   }
   return n;
}


void resetIntAccessor(IntAccessor* aa) {
   int i;
   int* curr = aa->curr_;
   int* max = aa->max_;

   aa->reset_ = 1;
   for (i = 0; i < aa->narrs_; i++) {
      if (*max <= 0) {
         aa->noComb_ = 1;
         return;
      }
      else {
         *curr = 0;
      }
      curr++;
   }
}

// Takes control of all input arrays
IntAccessor* createIntAccessor(int* max, int narrs) {
   IntAccessor* aa = (IntAccessor*)malloc(sizeof(IntAccessor));

   aa->noComb_ = 0;
   aa->max_ = max;
   aa->narrs_ = narrs;
   aa->curr_ = (int*)malloc(sizeof(int)*narrs);
   resetIntAccessor(aa);
   return aa;
}


void freeIntAccessor(IntAccessor* aa) {
   free(aa->max_);
   free(aa->curr_);
}

int* nextIntAccessor(IntAccessor* aa) {
   int i;
   if (aa->noComb_ == 1) return NULL;
   if (aa->reset_ == 1) {
      aa->reset_ = 0;
   }
   if (aa->curr_[0] < 0) return NULL;

   for (i = aa->narrs_-1; i >= 0; i--) {
      aa->curr_[i]++;
      if (aa->curr_[i] < aa->max_[i]) return aa->curr_;
      aa->curr_[i] = 0;
   }
   return aa->curr_;
}

int numCombinationsInIntAccessor(const IntAccessor* aa) {
   int n = 1;
   int i;
   if (aa->narrs_ == 0) return 0;
   for (i = 0; i < aa->narrs_; i++) {
      if (aa->max_[i] < 0) n = 0;
      else n *= aa->max_[i];
   }
   return n;
}
