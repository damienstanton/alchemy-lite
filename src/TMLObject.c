#include "TMLObject.h"
#include "util.h"


TMLObject* TMLObjectNew(int id, TMLClass* cl, char* name) {
   TMLObject* tmlobj = (TMLObject*)malloc(sizeof(TMLObject));
   tmlobj->id = id;
   tmlobj->cl = cl;
   tmlobj->final = 0;
   tmlobj->name = name;

   tmlobj->parent = NULL;
//   tmlobj->notClasses = g_sequence_new(NULL);
   tmlobj->numBannedClasses = 0;
   tmlobj->bannedClasses = NULL;
   tmlobj->relValues = NULL;
   tmlobj->part = NULL;
   tmlobj->wmc = -1;
   return tmlobj;
}
void clearChangedWMCPath(TMLObject* obj) {
   obj->wmc = -1;

   if (obj->parent != NULL)
      clearChangedWMCPath(obj->parent);
}

void freeTMLObject(void* obj) {
   TMLObject* tmlobj = (TMLObject*)obj;
   int i;
   for (i = 0; i < tmlobj->numcl; i++) {
      free(tmlobj->relValues[i]);
      free(tmlobj->part[i]);
   }
   free(tmlobj->relValues);
   free(tmlobj->part);
//   g_sequence_free(tmlobj->notClasses);
   free(tmlobj);
}
