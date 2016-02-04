#ifndef _TMLOBJECT_H__
#define _TMLOBJECT_H__

#include "TMLClass.h"

typedef struct TMLObject {
   int id;
   char* name;
   TMLClass* cl; /* most refined class of object, if given */

   int* bannedClasses; /* list of classes that the object is not */
   int numBannedClasses;
   struct TMLObject* parent;
   int numcl; /* num classes object can be */
   int** relValues; // relValues[class][class relations]
               /* int is 0 if false, 1 if true, 
                  not be in hashmap if unknown */
   struct TMLObject** part; /* part[class][class parts]*/
   float wmc;
   int final;
   UT_hash_handle hh; /* makes this structure hashable */
} TMLObject;

TMLObject* TMLObjectNew(int id, TMLClass* cl, char* name);

void clearChangedWMCPath(TMLObject* obj);
void freeTMLObject(void* obj);

#endif
