#ifndef _TMLCLASS_H__
#define _TMLCLASS_H__

#include <stdlib.h>
#include "uthash.h"
#include "util.h"

struct TMLClass;

typedef struct TMLAttrValue {
   char* name;
   float wt;
   int idx;
   UT_hash_handle hh;
} TMLAttrValue;

typedef struct TMLAttribute {
   char* name;
   TMLAttrValue* vals;
   int nvals;
   int defaultAttr;
   int* defaultAttrForSubcl;
   int idx;
   UT_hash_handle hh;
} TMLAttribute;

/* Stores information on one TML relation rule */
typedef struct TMLRelation {
   char* name; /* name of the relation */
   int nargs; /* how many arguments the relation is over */
   int* argClass; /* which class is arg part from, either the class the relation is in, or a super class (TODO: Or a subclass???) */
   char** argPartName; /* indexes into to the TMLClass's part array */
   int numposs; /* number of possible grounded relations this rule covers for an object */
   float pwt; /* positive log weight */
   float nwt; /* negative log weight */
   int pcnt; /* positive count */
   int ncnt; /* negative count */
   int hard; /* 1 if always pos, -1 if always neg, 0 otherwise */
   int defaultRel; /* 1 if subclasses override this relation, 0 otherwise */
   int* defaultRelForSubcl; /* defaultRelForSubcl[i] == 1 if some subparts of subcl i override this class
                           0 if none do */
   int overrideRel; /* If this relation overrides a superclass */
   UT_hash_handle hh; /* makes this structure hashable */

   int mapPcnt; /* positive count in MAP state */
   int mapNcnt; /* negative count in MAP state */
} TMLRelation;

/* Stores information about a TML subpart relation */
typedef struct TMLPart {
   char* name; /* name of the subpart */
   int n; /* number of parts with this subpart relation that class cl has */
   struct TMLClass* cl; /* class of the subpart */
   struct TMLClass* clOfOverriddenPart; /* class of the most recent subpart this part overrides */
   int defaultPart; /* 0 if no subclasses of the class this is a part of override this part, 
                       1 otherwise*/ 
   int* defaultPartForSubcl; /* defaultPartForSubcl[i] == 1 if some descendant of subcl i overrides this             , 1 otherwise */
   int overridePart; /* If this part overrides a superclass */
   int maxNumParts;
   int idx;
   UT_hash_handle hh; /* makes this structure hashable */
} TMLPart;

TMLRelation* TMLRelationNew(const char* name, int numParts, int numSubcl);
TMLRelation* copyTMLRelation(TMLRelation* rel);
void setUpTMLRelation(TMLRelation* rel, const char* name, int numParts, int numSubcl);
void freeTMLRelation(void* obj);
void freeTMLPart(void* obj);

/* Stores information about one class in the TML KB */
typedef struct TMLClass {
   int id; /* class id */
   char* name; /* class name */
   struct TMLClass* par; /* parent class*/
   int subclIdx; /* index of class in its parent's subcl array. -1 if no parent. */
   int nsubcls; /* number of subclasses */
   struct TMLClass** subcl; /* struct TMLClass* subclasses */
   float* wt; /* weight of subclasses */
   int* cnt; /* counts of the subclasses */
   int totalcnt; /* count of this class */
   int nparts; /* number of subparts */
   TMLPart* part; /* hashtable of struct TMLPart of subparts */
   int nrels;
   TMLRelation* rel; /* struct TMLRelations of all relations */
   int changed;
   int level;
   int isPart;
   UT_hash_handle hh; /* makes this structure hashable */
   int nattr; /* number of attributes */
   TMLAttribute* attr; /* hashtable of attributes for this class */

   int mapCnt; /* number of instances in MAP solution */
} TMLClass;

TMLClass* TMLClassNew(int id, int subclIdx);
void setUpTMLClass(TMLClass* cl, char* className, int subclIdx);
TMLClass* rootClass(TMLClass* cl);
void finalizeClass(TMLClass* cl);
void freeTMLClass(void* obj);
void updateClassLevel(TMLClass* cl);
TMLClass* getLowestAncestorForPart(TMLClass* cl, const char* part);
int isAncestor(TMLClass* a, TMLClass* cl);
int isDescendant(TMLClass* d, TMLClass* cl);
TMLPart* getPart(TMLClass* cl, const char* partName);
TMLAttribute* getAttribute(TMLClass* cl, const char* attrName);
TMLRelation* getRelation(TMLClass* cl, const char* relName);

#endif
