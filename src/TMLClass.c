#include "TMLClass.h"
#include <string.h>
#include "util.h"

/**
 * Creates a new TMLRelation struct 
 *
 * @param name      name of the relation
 * @param numParts  number of parts the relation has
 * @return          a new TMLRelation struct
 */
TMLRelation* TMLRelationNew(const char* name, int numParts, int numSubcl) {
   TMLRelation* tmlr = (TMLRelation*)malloc(sizeof(TMLRelation));
   int i;
   tmlr->name = strdup(name);
   tmlr->nargs = numParts;
   tmlr->argClass = (int*)malloc(sizeof(int)*numParts);
   tmlr->argPartName = (char**)malloc(sizeof(char*)*numParts);
   tmlr->pwt = 0.0;
   tmlr->nwt = 0.0;
   tmlr->pcnt = 0;
   tmlr->ncnt = 0;
   tmlr->hard = 0;
   tmlr->numposs = 1; 
   tmlr->defaultRel = 0;
   tmlr->overrideRel = 0;
   tmlr->defaultRelForSubcl = NULL;
   return tmlr;
}

/**
 * Sets up a TMLRelation struct
 *
 * @param rel        the TMLRelation pointer to set up
 * @param name       the name of the relation
 * @param numParts   number of parts the relation has
 */ 
void setUpTMLRelation(TMLRelation* rel, const char* name, int numParts, int numSubcl) {
   int i;
   rel->name = strdup(name);
   rel->nargs = numParts;
   rel->argClass = (int*)malloc(sizeof(int)*numParts);
   rel->argPartName = (char**)malloc(sizeof(char*)*numParts);
   rel->pwt = 0.0;
   rel->nwt = 0.0;
   rel->pcnt = 0;
   rel->ncnt = 0;
   rel->hard = 0;
   rel->numposs = 1;
   rel->defaultRel = 0;
   rel->overrideRel = 0;
   rel->defaultRelForSubcl = NULL;
}

TMLRelation* copyTMLRelation(TMLRelation* rel) {
   TMLRelation* newRel = (TMLRelation*)malloc(sizeof(TMLRelation));
   int i;
   int* pc = rel->argClass;
   char** pn = rel->argPartName;
   newRel->name = strdup(rel->name);
   newRel->nargs = rel->nargs;
   newRel->argClass = (int*)malloc(sizeof(int)*newRel->nargs);
   newRel->argPartName = (char**)malloc(sizeof(char*)*newRel->nargs);
   for (i = 0; i < newRel->nargs; i++) {
      newRel->argClass[i] = *pc;
      if (*pc == -1) newRel->argPartName[i] = strdup(*pn);
      else newRel->argPartName[i] = *pn;
      pc++;
      pn++;
   }
   newRel->pwt = rel->pwt;
   newRel->nwt = rel->nwt;
   newRel->pcnt = rel->pcnt;
   newRel->ncnt = rel->ncnt;
   newRel->hard = rel->hard;
   newRel->numposs = rel->numposs;
   newRel->defaultRel = rel->defaultRel;
   newRel->overrideRel = rel->overrideRel;
   newRel->defaultRelForSubcl = NULL;
   return newRel;
}

/**
 * frees a TMLRelation struct
 *
 * @param obj   pointer to the TMLRelation to free
 */
void freeTMLRelation(void* obj) {
   TMLRelation* tmlr = (TMLRelation*)obj;
   int i;
   free(tmlr->name);
   for (i = 0; i < tmlr->nargs; i++) {
      if (tmlr->argClass[i] == -1 && tmlr->argPartName[i] != NULL)
         free(tmlr->argPartName[i]);
   }
   free(tmlr->argClass);
   free(tmlr->argPartName);
   if (tmlr->defaultRelForSubcl != NULL)
      free(tmlr->defaultRelForSubcl); 
   free(tmlr);
}

/**
 * frees a TMLAttribute struct
 */
void freeTMLAttribute(void* obj) {
   TMLAttribute* attr = (TMLAttribute*)obj;
   TMLAttrValue* val;
   TMLAttrValue* tmp;
   free(attr->name);
   HASH_ITER(hh, attr->vals, val, tmp) {
      HASH_DEL(attr->vals, val);
      free(val->name);
      free(val);
   }
   if (attr->defaultAttrForSubcl != NULL)
      free(attr->defaultAttrForSubcl);
   free(attr);
}


/**
 * frees a TMLPart struct
 *
 * @param obj  pointer to the TMLPart to free
 */
void freeTMLPart(void* obj) {
   TMLPart* part = (TMLPart*)obj;
   free(part->name);
   if (part->defaultPartForSubcl != NULL) free(part->defaultPartForSubcl);
   free(part);
}

/**
 * Creates a new TMLClass struct
 *
 * @param id  id for this new class
 * @return  new TMLClass struct
 */
TMLClass* TMLClassNew(int id, int subclIdx) {
   TMLClass* tmlcl = (TMLClass*)malloc(sizeof(TMLClass));
   tmlcl->id = id;
   tmlcl->par = NULL;
   tmlcl->subclIdx = subclIdx;
   tmlcl->nsubcls = 0;
   tmlcl->subcl = NULL;
   tmlcl->wt = NULL;
   tmlcl->cnt = NULL;
   tmlcl->totalcnt = -1;
   tmlcl->nparts = 0;
   tmlcl->part = NULL;
   tmlcl->nrels = 0;
   tmlcl->rel = NULL;
   tmlcl->nattr = 0;
   tmlcl->attr = NULL;

   tmlcl->isPart = 0;
   tmlcl->changed = 1;
   tmlcl->level = 0;
   return tmlcl;
}

/**
 * Sets up a TMLClass struct
 *
 * @param cl        the TMLClass pointer to set up
 * @param className       the name of the class
 */ 
void setUpTMLClass(TMLClass* cl, char* className, int subclIdx) {
   cl->name = className;
   cl->par = NULL;
   cl->subclIdx = subclIdx;
   cl->nsubcls = 0;
   cl->subcl = NULL;
   cl->wt = NULL;
   cl->cnt = NULL;
   cl->totalcnt = -1;
   cl->nparts = 0;
   cl->part = NULL;
   cl->nrels = 0;
   cl->rel = NULL;
   cl->changed = 1;
   cl->level = 0;
   cl->isPart = 0;
   cl->nattr = 0;
   cl->attr = NULL;
}

/**
 * Finds the root class of the tree that class cl is in
 *
 * @param cl  class to find the root of
 * @return root class of cl's tree
 */
TMLClass* rootClass(TMLClass* cl) {
   if (cl->par == NULL) return cl;
   return rootClass(cl->par);
}

/**
 * Sets a class's level to 1 more than its parent
 * Recursively pushes changes down the class tree
 *
 * @param cl  root of class tree to update level of
 */
void updateClassLevel(TMLClass* cl) {
   int i;
   TMLClass* subcl;
   if (cl->par == NULL) return;
   cl->level = cl->par->level + 1;

   for (i = 0; i < cl->nsubcls; i++) {
      updateClassLevel(cl->subcl[i]);
   }
}

/**
 * Frees a TMLClass struct
 *
 * @param obj pointer to TMLClass to free
 */
void freeTMLClass(void* obj) {
   TMLClass* tmlc = (TMLClass*)obj;
   int i;
   TMLPart* part;
   TMLPart* tmppart;
   TMLRelation* rel;
   TMLRelation* tmprel;
   TMLAttribute* attr;
   TMLAttribute* tmpattr;

   free(tmlc->name);
   if (tmlc->subcl != NULL)
      free(tmlc->subcl);
   HASH_ITER(hh, tmlc->part, part, tmppart) {
      HASH_DEL(tmlc->part, part);
      freeTMLPart(part);
   }
   HASH_ITER(hh, tmlc->rel, rel, tmprel) {
      HASH_DEL(tmlc->rel, rel);
      freeTMLRelation(rel);
   }
   HASH_ITER(hh, tmlc->attr, attr, tmpattr) {
      HASH_DEL(tmlc->attr, attr);
      freeTMLAttribute(attr);
   }
   free(tmlc->wt);
   free(tmlc->cnt);
}

int isDescendant(TMLClass* d, TMLClass* cl) {
   if (d->par == NULL) return -1;
   if (d->level <= cl->level) return -1;
   if (d->par->id == cl->id) return d->subclIdx;
   else if (d->par != NULL)
      return isDescendant(d->par, cl);
   else
      return -1;
}

int isAncestor(TMLClass* a, TMLClass* cl) {
   if (a->id == cl->id) return 1;
   else if (cl->par != NULL)
      return isAncestor(a, cl->par);
   else
      return 0;
}

TMLPart* getPart(TMLClass* cl, const char* partName) {
   TMLPart* part;
   HASH_FIND_STR(cl->part, partName, part);
   return part;
}

TMLAttribute* getAttribute(TMLClass* cl, const char* attrName) {
   TMLAttribute* attr;
   HASH_FIND_STR(cl->attr, attrName, attr);
   return attr;
}

TMLRelation* getRelation(TMLClass* cl, const char* relName) {
   TMLRelation* rel;
   HASH_FIND_STR(cl->rel, relName, rel);
   return rel;
}
