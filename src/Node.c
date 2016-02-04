
#include <stdio.h>
#include "Node.h"
#include "TMLClass.h"

#define MAX_NAME_LENGTH 100

TMLObject* newTMLObject(char* name) {
   TMLObject* obj = (TMLObject*)malloc(sizeof(TMLObject));
   obj->name = strdup(name);
   obj->paths = NULL;
   return obj;
}

void addPathToObject(TMLObject* obj, Node* node) {
   QNode* qn = (QNode*)malloc(sizeof(QNode));
   qn->ptr = node;
   qn->next = obj->paths;
   obj->paths = qn;
}


void initializeNode(Node* node, TMLClass* cl, char* name) {
   int i, j;
   TMLPart* part;
   TMLPart* tmppart;
   TMLRelation* rel;
   TMLRelation* tmp;
   TMLAttribute* attr;
   TMLAttribute* tmpa;
   int count;
   if (node == NULL) {
      printf("Error: NULL pointer passed to initialize node.\n");
      return;
   }
   if (node->name != NULL) return;
   node->cl = cl;
   if (name != NULL && strchr(name, '.') == NULL)
      node->name = name;
   else
      node->name = NULL;
   node->pathname = NULL;
   i = 0;
   node->part = (Node***)malloc(sizeof(Node**)*cl->nparts);
   HASH_ITER(hh, cl->part, part, tmppart) {
      node->part[i] = (Node**)malloc(sizeof(Node*)*part->n);
      for (j = 0; j < part->n; j++)
         node->part[i][j] = NULL;
      i++;
   }
   i = 0;
   node->relValues = (int**)malloc(sizeof(int*)*cl->nrels);
   HASH_ITER(hh, cl->rel, rel, tmp) {
      node->relValues[i] = (int*)malloc(sizeof(int)*3);
      if (rel->numposs != -1) {
         node->relValues[i][0] = 0;
         node->relValues[i][1] = 0;
         node->relValues[i][2] = rel->numposs;
      }
      i++;
   }
   node->assignedAttr = (TMLAttrValue**)malloc(sizeof(TMLAttrValue*)*cl->nattr);
   for (i = 0; i < cl->nattr; i++)
      node->assignedAttr[i] = NULL;
   node->attrValues = (int**)malloc(sizeof(int*)*cl->nattr);
   for (i = 0; i < cl->nattr; i++)
      node->attrValues[i] = NULL;
   node->assignedSubcl = -1;
   node->subclMask= NULL;
   node->subcl = NULL;
   node->changed = 1;
   node->par = NULL;
   node->npars = 0;
   node->active = 0;
   node->nmaxgroundliterals = 0;
}

/**
 * Frees all the nodes in a tree beginning at node.
 *
 * NOTE: This will need to be revised when a node can be the
 * subpart of more than one other node.
 */
void freeTreeRootedAtNode(Node* node) {
   int i, j;
   TMLClass* cl = node->cl;
   Node* subcl = node->subcl;
   Node** part;

   if (node->subcl != NULL) {
      if (node->assignedSubcl != -1 && node->subclMask == NULL)
         freeTreeRootedAtNode(subcl);
      else {
         for (i = 0; i < cl->nsubcls; i++) {
            if (node->subclMask != NULL && node->subclMask[i] == 0) {
               subcl++; continue;
            }
            freeTreeRootedAtNode(subcl);
            subcl++;
         }
      }
      free(node->subcl);
   }
   for (j = 0; j < cl->nparts; j++) {
      part = node->part[j];
      free(part);
   }
   free(node->part);
   for (i = 0; i < cl->nrels; i++) {
      free(node->relValues[i]);
   }
   free(node->relValues);
   free(node->assignedAttr);
   for (i = 0; i < cl->nattr; i++) {
      if (node->attrValues[i] != NULL) free(node->attrValues[i]);
   }
   free(node->attrValues);
   if (node->subclMask != NULL)
      free(node->subclMask);
   if (node->par != NULL) free(node->par);
}

void addParent(Node* node, Node* par) {
   Node** newArr;
   if (node->par == NULL) {
      node->par = (Node**)malloc(sizeof(Node*));
      *(node->par) = par;
      node->npars = 1;
   } else {
      newArr = (Node**)malloc(sizeof(Node*)*(node->npars+1));
      memcpy(newArr, node->par, (sizeof(Node*)*node->npars));
      newArr[node->npars] = par;
      free(node->par);
      node->par = newArr;
      node->npars++;
   }
}

void markNodeAncestorsAsActive(Node* node) {
   Node** par = node->par;
   int p;
   if (node->active == 1) return;
   node->active = 1;
   for (p = 0; p < node->npars; p++) {
      markNodeAncestorsAsActive(*par);
      par++;
   }
}
