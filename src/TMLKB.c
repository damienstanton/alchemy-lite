#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "TMLKB.h"
#include "util.h"
#include "Node.h"

#define MAX_LINE_LENGTH 10000
#define MAX_NAME_LENGTH 1000
#define TRUE 1
#define FALSE 0

#define SUBCLASS 0
#define SUBPART 1
#define RELATION 2
#define ATTRIBUTE 3
#define EMPTY_LINE -1
#define ERROR -2

#define relWeight(relVals,rel) (rel->hard == 0 ? (((relVals[0] != 0) ? relVals[0]*rel->nwt : 0.0) \
   +((relVals[1] != 0) ? relVals[1]*rel->pwt : 0.0) \
   +((relVals[2] != 0) ? relVals[2]*logsum_float(rel->pwt,rel->nwt) : 0.0)) : \
   ((rel->hard == 1) ? (relVals[0] != 0 ? log(0.0) : 0.0) : (relVals[1] != 0 ? log(0.0) : 0.0)))

float attrWeight(Node* node, TMLAttribute* attr) {
   TMLAttrValue* attrvalue;
   TMLAttrValue* tmp;
   float* args;
   float sum;
   int* mask = node->attrValues[attr->idx];
   if (node->assignedAttr[attr->idx] != NULL) {
      return node->assignedAttr[attr->idx]->wt;
   }
   args = (float*)malloc(sizeof(float)*attr->nvals);
   
   if (mask == NULL) { 
      HASH_ITER(hh, attr->vals, attrvalue, tmp) {
         args[attrvalue->idx] = attrvalue->wt;
      }
   } else {
      HASH_ITER(hh, attr->vals, attrvalue, tmp) {
         if (mask[attrvalue->idx] < 0) {
            args[attrvalue->idx] = log(0.0);
         } else {
            args[attrvalue->idx] = attrvalue->wt;
         }
      }
   }
   sum = logsumarr_float(args, attr->nvals);
   free(args);
   return sum;
}

/**
 * Sets up a new TMLKB struct
 */
TMLKB* TMLKBNew() {
   TMLKB* kb = (TMLKB*)malloc(sizeof(TMLKB));

   kb->root = NULL;
   kb->classes = NULL;
   kb->classNameToPtr = NULL;
   kb->objectNameToPtr = NULL;
   kb->objectPathToPtr = NULL;
   kb->classToObjPtrs = NULL;

   kb->objToRelFactStrs = NULL;

   kb->topcl = NULL;
   kb->numClasses = 0;

   kb->logZ = 0.0;
   kb->edits = NULL;
   kb->mapSet = 0;

   // Learning parameters
   // TODO: read these from somewhere
   kb->scPct = 1;
   kb->relPctT = 1;
   kb->relPctF = 1;
   kb->l0 = 1.0;
   kb->l1 = 1.0;

   return kb;
}

/**
 * Adds a KBEdit (i.e., information regarding a change to the TML KB)
 * to the KBEdit stack.
 *
 * @param node         node in the SPN the change is related to
 * @param subclIdx     If a change of a subclass of node's class, the index of that subclass
 * @param relStr       If an added relation fact, the name of the relation
 * @param relIdx       If an added relation fact, the index of the relation in the array of rels
 * @param pol          Polarity of the change
 * @param prev         Stack of KBEdits to push onto
 * @return the updated KBEdit stack
 */
KBEdit* addKBEdit(Node* node, int subclIdx, char* relStr, int relIdx, int valIdx, int pol, KBEdit* prev) {
   KBEdit* newEdit = (KBEdit*)malloc(sizeof(KBEdit));
   newEdit->prev = prev;
   newEdit->node = node;
   newEdit->subclIdx = subclIdx;
   newEdit->relStr = relStr;
   newEdit->relIdx = relIdx;
   newEdit->valIdx = valIdx;
   newEdit->pol = pol;
   return newEdit;
}

KBEdit* addAndCopyKBEdit(KBEdit* copy, KBEdit* prev) {
   KBEdit* newEdit = (KBEdit*)malloc(sizeof(KBEdit));
   newEdit->prev = prev;
   newEdit->node = copy->node;
   newEdit->subclIdx = copy->subclIdx;
   newEdit->relStr = copy->relStr;
   newEdit->relIdx = copy->relIdx;
   newEdit->valIdx = copy->valIdx;
   newEdit->pol = copy->pol;
   return newEdit;
}

/**
 * Splits a string of the form %s.%d into its string and number parts
 * If string is not of that form, returns a copy of the string and
 * num is set to 1.
 * 
 * @param str  string to split
 * @param num  stores the number %d that was split in the string
 * @return the %s part of str
 */
char* findBasePartName(char* str, int* num) {
   char buffer[MAX_NAME_LENGTH+1];
   int correctScan = sscanf(str, "%[^][][%d]", buffer, num);
   if (correctScan == 1) {
      *num = 1;
      return strdup(buffer);
   } else if (correctScan == 0) {
      *num = 1;
      return strdup(str);
   }
   return strdup(buffer);
}


/**
 * Create the shortest pathname for a node by finding its
 * closest ancestor with a name and starting the pathname from there.
 */
char* createBestPathname(TMLKB* kb, Node* node) {
   char* period = strrchr(node->pathname, '.');
   Node* par;
   char* newper;
   char best[MAX_NAME_LENGTH+1];
   char partRel[MAX_NAME_LENGTH+1];
   TMLPart* part = NULL;
   TMLClass* cl;
   Node* tmpnode;

   while (period != NULL) {
      *period = '\0';
      HASH_FIND(hh_path, kb->objectPathToPtr, node->pathname, strlen(node->pathname), par);
      if (par == NULL)
         HASH_FIND(hh, kb->objectNameToPtr, node->pathname, strlen(node->pathname), par);
      if (par->name != NULL) {
         sscanf(period+1, "%[^[]", partRel);
         cl = node->cl;
         tmpnode = node;
         while (TRUE) {
            part = getPart(cl, partRel);
            if (part != NULL) break;
            if (cl->nsubcls == 0 || tmpnode->assignedSubcl == -1) break;
            if (tmpnode->subclMask == NULL) tmpnode = tmpnode->subcl;
            else tmpnode = &(tmpnode->subcl[tmpnode->assignedSubcl]);
            cl = tmpnode->cl;
         }
         if (part != NULL && part->maxNumParts == 1) {
            newper = strrchr(node->pathname, '[');
            if (newper == NULL) {
               sprintf(best, "%s.%s", par->name, period+1);
               *period = '.';
               return strdup(best);
            } else {
               *newper = '\0';
               sprintf(best, "%s.%s", par->name, period+1);
               *period = '.';
               *newper = '[';
               return strdup(best);
            }
         }
         sprintf(best, "%s.%s", par->name, period+1);
         *period = '.';
         return strdup(best);
      }
      newper = strrchr(node->pathname, '.');
      *period = '.';
      period = newper;
   }
   sscanf(period+1, "%[^[]", partRel);
   cl = node->cl;
   tmpnode = node;
   while (TRUE) {
      part = getPart(cl, partRel);
      if (part != NULL) break;
      if (cl->nsubcls == 0 || tmpnode->assignedSubcl == -1) break;
      if (tmpnode->subclMask == NULL) tmpnode = tmpnode->subcl;
      else tmpnode = &(tmpnode->subcl[tmpnode->assignedSubcl]);
      cl = tmpnode->cl;
   }
   if (part != NULL && part->maxNumParts == 1) {
      newper = strrchr(node->pathname, '[');
      if (newper == NULL) {
         return strdup(node->pathname);
      } else {
         *newper = '\0';
         period = strdup(node->pathname);
         *newper = '[';
         return period;
      }
   } else return strdup(node->pathname);
}

char* createNamedRelStr(TMLKB* kb, Node* node, char* normalizedStr) {
   char* partName = strchr(normalizedStr, '(');
   char* comma = strchr(normalizedStr, ',');
   char* newcomma;
   Node* part;
   int end = 0;
   char ret_even[MAX_LINE_LENGTH+1];
   char ret_odd[MAX_LINE_LENGTH+1];
   char pathname[MAX_NAME_LENGTH+1];
   int odd = 1;
   if (partName == NULL) return strdup(normalizedStr);

   *partName = '\0';
   if (partName == NULL) return strdup(normalizedStr);

   if (comma == NULL) {
      comma = strchr(normalizedStr, ')');
      if (comma == NULL) return strdup(normalizedStr);
      *comma = '\0';
      partName++;
      if (partName == NULL) {
         sprintf(ret_even, "%s()", normalizedStr);
         partName--;
         *partName = '(';
         *comma = ')';
         return strdup(ret_even);
      }
      HASH_FIND(hh_path, kb->objectPathToPtr, partName, strlen(partName), part);
      if (part == NULL) {
         if (node->pathname != NULL)
            sprintf(pathname, "%s.%s", node->pathname, partName);
         else
            sprintf(pathname, "%s.%s", node->name, partName);
         HASH_FIND(hh_path, kb->objectPathToPtr, pathname, strlen(pathname), part);
      }
      if (part == NULL) {
         partName--;
         *partName = '(';
         *comma = ')';
         return strdup(normalizedStr);
      }
      if (part->name != NULL && strchr(part->name, '.') == NULL)
         sprintf(ret_even, "%s(%s)", normalizedStr, part->name);
      else
         sprintf(ret_even, "%s(%s)", normalizedStr, partName);
      partName--;
      *partName = '(';
      *comma = ')';
      return strdup(ret_even);
   }
   sprintf(ret_even, "%s(", normalizedStr);

   *partName = '(';
   partName++;
   do {
      *comma = '\0';
      HASH_FIND(hh_path, kb->objectPathToPtr, partName, strlen(partName), part);
      if (part == NULL) {
         if (node->pathname != NULL)
            sprintf(pathname, "%s.%s", node->pathname, partName);
         else
            sprintf(pathname, "%s.%s", node->name, partName);
         HASH_FIND(hh_path, kb->objectPathToPtr, pathname, strlen(pathname), part);
      }
      if (part == NULL) {
         if (end == 0) *comma = ',';
         else *comma = ')';
         return strdup(normalizedStr);   
      }
      if (odd == 1) {
         if (end == 0) {
            sprintf(ret_odd, "%s%s,", ret_even, (part->name == NULL || strchr(part->name, '.') != NULL) ? partName : part->name);
         } else {
            sprintf(ret_odd, "%s%s)", ret_even, (part->name == NULL || strchr(part->name, '.') != NULL) ? partName : part->name);
         }
         odd = 0;
      } else {
         if (end == 0) {
            sprintf(ret_even, "%s%s,", ret_odd, (part->name == NULL || strchr(part->name, '.') != NULL) ? partName : part->name);
         } else {
            sprintf(ret_even, "%s%s)", ret_odd, (part->name == NULL || strchr(part->name, '.') != NULL) ? partName : part->name);
         }
         odd = 1;
      }
      partName = comma+1;
      if (end == 0) *comma = ',';
      else *comma = ')';
      if (end == 0) {
         comma++;
         newcomma = strchr(comma, ',');
         if (newcomma == NULL) {
            newcomma = strchr(comma, ')');
            if (newcomma == NULL) {
               return strdup(normalizedStr);
            }
            end = 1;
         }
         comma = newcomma;
      } else break;
   } while (TRUE);
   if (odd == 1)
      return strdup(ret_even);
   else
      return strdup(ret_odd);
}

/**
 * Takes a relation name, object, and a set of argument nodes and creates a normalized string
 * of that relation (e.g., the whole relation literal with no spaces).
 * Ex. relation = "Parent", object="Smiths", argNodes=[Node(Anna,Person),Node(Bob,Person)]
 * If addBase == 1, return "Parent(Smiths,Anna,Bob)"
 * If addBase == 0, return "Parent(Anna,Bob)
 *
 * @param kb         TML KB
 * @param relation   name of the relation
 * @param object     name of the object
 * @param argNodes   list of pointers to the nodes of the arguments of the relation literal
 * @param nargs      length of argNodes array
 * @param addBase    determines if the object name should be added to the relation string
 * @return     normalized string of the relation ground literal
 */
char* createNormalizedRelStr(TMLKB* kb, char* relation, char* object, Node** argNodes, int nargs, int addBase, int useNames) {
   int a;
   char ret_even[MAX_LINE_LENGTH+1];
   char ret_odd[MAX_LINE_LENGTH+1];
   char* partName;
//   Node* obj;
   char* partPtr;

   if (nargs == 0) {
      if (addBase == 1)
         snprintf(ret_even, MAX_LINE_LENGTH, "%s(%s)", relation, object);
      else
         snprintf(ret_even, MAX_LINE_LENGTH, "%s()", relation);
      return strdup(ret_even);
   }
   if (addBase == 1)
      sprintf(ret_even, "%s(%s,", relation, object);
   else
      sprintf(ret_even, "%s(", relation);
   for (a = 0; a < nargs; a++) {
      if (useNames != 1 || argNodes[a]->name == NULL)
         partName = argNodes[a]->pathname;
      else
         partName = argNodes[a]->name;
      
      partPtr = strrchr(partName, '.');
      if (partPtr == NULL) partPtr = partName;
      else partPtr++;
      if (strchr(partName, '[') != NULL || (useNames == 1 && strchr(partName, '.') == NULL)) {
         if (a % 2 == 1) {
            if (a != nargs-1)
               sprintf(ret_even, "%s%s,", ret_odd, partPtr);
            else
               sprintf(ret_even, "%s%s)", ret_odd, partPtr);
         } else {
            if (a != nargs-1)
               sprintf(ret_odd, "%s%s,", ret_even, partPtr);
            else
               sprintf(ret_odd, "%s%s)", ret_even, partPtr);
         }
      } else {
         if (a % 2 == 1) {
            if (a != nargs-1)
               sprintf(ret_even, "%s%s[1],", ret_odd, partPtr);
            else
               sprintf(ret_even, "%s%s[1])", ret_odd, partPtr);
         } else {
            if (a != nargs-1)
               sprintf(ret_odd, "%s%s[1],", ret_even, partPtr);
            else
               sprintf(ret_odd, "%s%s[1])", ret_even, partPtr);
         }
      }
   }
   if (nargs % 2 == 1) return strdup(ret_odd);
   return strdup(ret_even);
}

/**
 * Splits a relation rule/fact string into its argument's strings.
 * Returns the normalized version of relStr where all the whitespace is removed.
 *
 * @param relStr     string of the relation's arguments
 * @param relation   string of relation name
 * @param nargs      returns the number of arguments found
 * @param args       returns an array of the argument strings
 * @return the normalized string
 */
char* splitRelArgsAndCreateNormalizedRelStr(TMLKB* kb, char* relStr, char* relation, int* nargs, char*** args, int useNames) {
   int n = 0;
   char obj_fmt_str[25];
   char* iter = relStr;
   int correctScan;
   char obj[MAX_NAME_LENGTH+1];
   char ret_even[MAX_LINE_LENGTH+1];
   char ret_odd[MAX_LINE_LENGTH+1];
   int a = 0;
   Node* node;
   char* name;
   char* partPtr;

   // If relStr is NULL, the relation has no arguments
   if (relStr == NULL) {
      snprintf(ret_even, MAX_LINE_LENGTH, "%s()", relation);
      *nargs = 0;
      *args = NULL;
      return strdup(ret_even);
   }
   sprintf(ret_even, "%s(", relation);
   snprintf(obj_fmt_str, 25, " %%%d[]a-zA-Z0-9.[:?] ", MAX_NAME_LENGTH);

   // Counts how many arguments there are
   while (TRUE) {
      iter = strchr(iter, ',');
      if (iter != NULL) { iter++; n++; }
      else {n++; break; }
   }
   *nargs = n;
   *args = (char**)malloc(n*sizeof(char*));
   iter = relStr;
   // Adds arguments to the normalized string as they are read in
   for (a = 0; a < n; a++) {
      correctScan = sscanf(iter, obj_fmt_str, obj);
      if (correctScan != 1) {
         return NULL;
      }
      HASH_FIND_STR(kb->objectNameToPtr, obj, node);
      
      if (node == NULL) {
         node = findNodeFromAnonName(kb, NULL, obj, 0);
         if (node == NULL || node->pathname == NULL || useNames == 1)
            name = obj;
         else {
            name = node->pathname;
            partPtr = strrchr(name, '.');
            if (partPtr == NULL) partPtr = name;
            else partPtr++;
            name = partPtr;
         }
      } else if (node->pathname == NULL || useNames == 1) {
            name = obj;
      } else {
         name = node->pathname;
         partPtr = strrchr(name, '.');
         if (partPtr == NULL) partPtr = name;
         else partPtr++;
         name = partPtr;
      }
      (*args)[a] = strdup(obj);
      if (a % 2 == 1) {
         if (a != n-1)
            sprintf(ret_even, "%s%s,", ret_odd, name);
         else
            sprintf(ret_even, "%s%s)", ret_odd, name);
      } else {
         if (a != n-1)
            sprintf(ret_odd, "%s%s,", ret_even, name);
         else
            sprintf(ret_odd, "%s%s)", ret_even, name);
      }
      iter = strchr(iter, ',');
      if (iter != NULL) iter++;
   }
   if (n % 2 == 1) return strdup(ret_odd);
   return strdup(ret_even);
}

/**
 * Takes a path name through the TML KB and finds the node associated with it
 * 
 * @param kb      TML KB
 * @param base    initial node in the path
 * @param name    path name
 * @param init    If init == 1, initialize nodes along the path (and the found node)
 * @return the node referred to by the path
 */
Node* findNodeFromAnonName(TMLKB* kb, Node* base, const char* name, int init) {
   char* period = strchr(name, '.');
   char* nextperiod;
   Node* node;
   char part_fmt_str[50];
   char part_fmt_str2[50];
   char partname[MAX_NAME_LENGTH+1];
   int partnum = -1;
   int correctScan;
   TMLPart* part;
   int maxParts = -1; // unnecessary. needed for findPartDown()

   // If there is no period in the name, then name refers to either
   //    (1) an object
   //    (2) a part name of the base object
   snprintf(part_fmt_str, 50, "%%%d[^][][%%d]", MAX_NAME_LENGTH);
   snprintf(part_fmt_str2, 50, "%%%d[^][]", MAX_NAME_LENGTH);
   if (period == NULL) {
      HASH_FIND_STR(kb->objectNameToPtr, name, node);
      if (node == NULL && base != NULL) {
         correctScan = sscanf(name, part_fmt_str, partname, &partnum);
         if (correctScan == 2) {
            if (init == 1)
               node = findAndInitPartDown(kb, base, partname, partnum, base->name);
            else
               node = findPartDown(base, partname, partnum, 0, &maxParts);
         } else {
            correctScan = sscanf(name, part_fmt_str2, partname);
            partnum = 1;
            if (correctScan == 1) {
               if (init == 1)
                  node = findAndInitPartDown(kb, base, partname, partnum, base->name);
               else
                  node = findPartDown(base, partname, partnum, 0, &maxParts);
            }
         }
      }
      return node;
   }
   *period = '\0';
   HASH_FIND_STR(kb->objectNameToPtr, name, node);
   if (node == NULL) {
      *period = '.';
      return NULL;
   }

   // find next node in path for each "." in the name
   do {
      nextperiod = strchr(period+1, '.');
      if (nextperiod == NULL) {
         correctScan = sscanf(period+1, part_fmt_str, partname, &partnum);
         if (correctScan == 2) {
            if (init == 1) {
               if (node->pathname == NULL)
                  node = findAndInitPartDown(kb, node, partname, partnum, node->name);
               else
                  node = findAndInitPartDown(kb, node, partname, partnum, node->pathname);
            } else
               node = findPartDown(node, partname, partnum, 0, &maxParts);
         } else {
            if (init == 1) {
               if (node->pathname == NULL)
                  node = findAndInitPartDown(kb, node, partname, 1, node->name);
               else
                  node = findAndInitPartDown(kb, node, partname, 1, node->pathname);
            } else
               node = findPartDown(node, partname, 1, 0, &maxParts);
         }
         *period = '.';
         return node;
      }
      *nextperiod = '\0';
      correctScan = sscanf(period+1, part_fmt_str, partname, &partnum);
      if (correctScan == 2) {
         if (init == 1) {
            if (node->pathname == NULL)
               node = findAndInitPartDown(kb, node, partname, partnum, node->name);
            else
               node = findAndInitPartDown(kb, node, partname, partnum, node->pathname);
         } else
            node = findPartDown(node, partname, partnum, 0, &maxParts);
      } else {
         if (init == 1) {
            if (node->pathname == NULL)
               node = findAndInitPartDown(kb, node, partname, 1, node->name);
            else
               node = findAndInitPartDown(kb, node, partname, 1, node->pathname);
         } else
            node = findPartDown(node, partname, 1, 0, &maxParts);
      }
      *period = '.';
      if (node == NULL) {
         *nextperiod = '.';
         return NULL;
      }
      period = nextperiod;
   } while (TRUE);
}

/**
 * Recursive helper method that updates the class of an object in the SPN.
 *
 * Recursively creates nodes for new known classes of an object.
 * If tmlFactFile is null, then this is being called from interactive mode to add a new fact.
 * In this case, a KBEdit is added to the stack of edits kb->edits.
 *
 * @param kb            TMLKB struct
 * @param name          name of the object
 * @param topNode       node for this object with the previously finest class information known
 * @param cl            current class for the object we are updating
 * @param finecl        new class information that we know (i.e., Is(name,finecl) )
 * @param tmlFactFile   file with TML Facts (needed to close the file if an error occurs)
 * @param linenum       current line number of file (needed for errors)
 * @return pointer for the object:subcl node
 */
Node* updateClassForNodeRecHelper(TMLKB* kb, KBEdit** editPtr, char* name, Node* topNode, TMLClass* cl, TMLClass* finecl, FILE* tmlFactFile, int linenum) {
   int i, j;
   Node* ret = NULL;
   Node* obj;
   Node* par;
   int numRel;
   TMLPart* part;
   TMLPart* tmppart;
   TMLRelation* rel;
   TMLRelation* tmprel;
   int* subclMask;
   QNode* qnode;
   QNode* classToObjPtrsList;
   KBEdit* edits = (editPtr == NULL) ? NULL : *editPtr;

   // If the class's parent is the same as the previously known finest class information
   // for the object, create a node for the object:cl pair.
   // Otherwise, recursively create the next coarsest node and return the pointer to
   // the node for the object:cl pair.
   if (cl->par == topNode->cl) {
      if (topNode->subcl == NULL) {
         topNode->subcl = (Node*)malloc(sizeof(Node));
         topNode->subclMask = NULL;
         topNode->changed = 1;
         obj = topNode->subcl;
         obj->par = NULL;
         obj->name = NULL;
         addParent(obj, topNode);
      } else {
         if (topNode->subclMask == NULL) {
             topNode->subclMask = (int*)malloc(sizeof(int)*topNode->cl->nsubcls);
             subclMask = topNode->subclMask;
             for (i = 0; i < topNode->cl->nsubcls; i++) {
                *subclMask = 1;
                subclMask++;
             }
         }
         topNode->assignedSubcl = cl->subclIdx;
         obj = &(topNode->subcl[cl->subclIdx]);
         if (editPtr != NULL) {
            edits = addKBEdit(topNode, cl->subclIdx, NULL, -1, -1, 1, edits);
            classToObjPtrsList = kb->classToObjPtrs[cl->id];
            qnode = (QNode*)malloc(sizeof(QNode));
            qnode->ptr = obj; 
            qnode->next = classToObjPtrsList;
            kb->classToObjPtrs[cl->id] = qnode;
         }
         if (editPtr != NULL) *editPtr = edits;
         return obj;
      }
   } else if (cl->par->level == topNode->cl->level && cl->par != topNode->cl) {
      if (tmlFactFile == NULL) {
         printf("Error on line %d in fact file: Object %s has been assigned two mismatching classes: %s and %s.\n", linenum, name, topNode->cl->name, finecl->name);
         freeTMLKB(kb);
         fclose(tmlFactFile); 
         exit(1);
      }
      printf("Object %s is of class %s which conflicts with new class information.\n", name, topNode->cl->name);
      return NULL;
   } else obj = updateClassForNodeRecHelper(kb, editPtr, name, topNode, cl->par, finecl, tmlFactFile, linenum);

   if (obj == NULL) return NULL;  
   if (obj->name != NULL) {
      if (cl == finecl) return obj;
      if (obj->cl->par != NULL) {
         par = *(obj->par);
         par->assignedSubcl = cl->subclIdx;
         if (par->subclMask == NULL) {
            par->subclMask = (int*)malloc(sizeof(int)*par->cl->nsubcls);
            subclMask = par->subclMask;
            for (i = 0; i < par->cl->nsubcls; i++) {
               *subclMask = 1;
               subclMask++;
            }
         }
         if (editPtr != NULL) {
            kb->edits = addKBEdit(par, cl->subclIdx, NULL, -1, -1, 1, kb->edits);
            classToObjPtrsList = kb->classToObjPtrs[cl->id];
            qnode = (QNode*)malloc(sizeof(QNode));
            qnode->ptr = obj; 
            qnode->next = classToObjPtrsList;
            kb->classToObjPtrs[cl->id] = qnode;
         }
         if (editPtr != NULL) *editPtr = edits;
         return &(par->subcl[cl->subclIdx]);
      }
      return obj;
   } 
   // Set up obj:cl node 
   obj->cl = cl;
   if (strchr(name, '.') == NULL)
      obj->name = name;
   obj->changed = 1;
   i = 0;
   obj->part = (Node***)malloc(sizeof(Node**)*cl->nparts);
   HASH_ITER(hh, cl->part, part, tmppart) {
      obj->part[i] = (Node**)malloc(sizeof(Node*)*part->n);
      for (j = 0; j < part->n; j++)
         obj->part[i][j] = NULL;
      i++;
   }
   obj->relValues = (int**)malloc(sizeof(int*)*cl->nrels);
   i = 0;
   HASH_ITER(hh, cl->rel, rel, tmprel) {
      obj->relValues[i] = (int*)malloc(sizeof(int)*3);
      if (rel->hard == 0) {
         obj->relValues[i][0] = 0;
         obj->relValues[i][1] = 0;
         obj->relValues[i][2] = rel->numposs;
      } else if (rel->hard == 1) {
         obj->relValues[i][0] = 0;
         obj->relValues[i][1] = rel->numposs;
         obj->relValues[i][2] = 0;
      } else {
         obj->relValues[i][0] = rel->numposs;
         obj->relValues[i][1] = 0;
         obj->relValues[i][2] = 0;
      }
      i++;
   }
   obj->assignedAttr = (TMLAttrValue**)malloc(sizeof(TMLAttrValue**)*cl->nattr);  
   for (i = 0; i < cl->nattr; i++)
      obj->assignedAttr[i] = NULL;
   obj->attrValues = (int**)malloc(sizeof(int*)*cl->nattr);  
   for (i = 0; i < cl->nattr; i++)                        
      obj->attrValues[i] = NULL;
   if (cl == finecl) {
      obj->subcl = NULL;
      obj->assignedSubcl = -1;
      obj->subclMask = NULL;
      ret = obj;
   } else {
      obj->subcl = (Node*)malloc(sizeof(Node));
      addParent(obj->subcl, obj);
      obj->subcl->name = NULL;
      obj->assignedSubcl = -1;
      ret = obj->subcl;
      obj->subclMask = NULL;
   }
   if (obj->cl->par != NULL) (*(obj->par))->assignedSubcl = cl->subclIdx;

   if (editPtr != NULL) *editPtr = edits;
   return ret;
}


/**
 * Recursive helper method that updates the SPN from a !Is(X,C) fact
 *
 * Recursively creates nodes for new known classes of an object.
 * If tmlFactFile is null, then this is being called from interactive mode to add a new fact.
 * In this case, a KBEdit is added to the stack of edits kb->edits.
 *
 * @param kb            TMLKB struct
 * @param name          name of the object
 * @param topNode       node for this object with the previously finest class information known
 * @param cl            current class for the object we are updating
 * @param finecl        new class information that we know (i.e., !Is(name,finecl) )
 * @param tmlFactFile   file with TML Facts (needed to close the file if an error occurs)
 * @param linenum       current line number of file (needed for errors)
 * @return pointer for the object:subcl node
 */
Node* blockClassForNodeRecHelper(TMLKB* kb, KBEdit** editPtr, char* name, Node* topNode, TMLClass* cl, TMLClass* finecl, FILE* tmlFactFile, int linenum) {
   int i, j;
   Node* ret = NULL;
   Node* obj;
   Node* subcl;
   int numRel;
   TMLPart* part;
   TMLPart* tmppart;
   TMLRelation* rel;
   TMLRelation* tmprel;
   int* subclMask;
   KBEdit* edits = editPtr == NULL ? NULL : *editPtr;

   if (cl->par == topNode->cl) {
      if (topNode->subcl == NULL) {
         topNode->subcl = (Node*)malloc(sizeof(Node)*topNode->cl->nsubcls);
         topNode->subclMask = (int*)malloc(sizeof(int)*topNode->cl->nsubcls);
         for (i = 0; i < topNode->cl->nsubcls; i++) {
            topNode->subclMask[i] = 1;
            topNode->subcl[i].par = NULL;
            addParent(&(topNode->subcl[i]), topNode);
            topNode->subcl[i].changed = 1;
            topNode->subcl[i].name = NULL;
         }
      } else {
         if (topNode->subclMask == NULL) {
            topNode->subclMask = (int*)malloc(sizeof(int)*topNode->cl->nsubcls);
            subclMask = topNode->subclMask;
            for (i = 0; i < topNode->cl->nsubcls; i++) {
               *subclMask = 1;
               subclMask++;
            }
         }
      }
      if (finecl->par == topNode->cl) {
         j = 0;
         for (i = 0; i < topNode->cl->nsubcls; i++) {
            subcl = &(topNode->subcl[i]);
            if (subcl->cl == finecl) {
               if (tmlFactFile == NULL) {
                  if (topNode->subclMask[i] < 0) {
                     printf("Nothing done. Class already blocked from previous evidence.\n");
                     return NULL;
                  }
                  else topNode->subclMask[i] = -1;
               } else
                  topNode->subclMask[i] = 0;
            } else {
               if (topNode->subclMask[i] == 1) j = 1;
            }
         }
         if (j == 0) {
            if (tmlFactFile != NULL) {
               printf("Error on line %d in fact file: Blocked classes block all subclasses of class %s.\n",
                 linenum, topNode->cl->name);
               return NULL;
            } else {
               printf("Nothing done. Blocked classes block all subclasses of class %s.\n",
                  topNode->cl->name);
               topNode->subclMask[finecl->subclIdx] = 1;
               return NULL;
            }
         }
         if (editPtr == NULL)
            edits = addKBEdit(topNode, finecl->subclIdx, NULL, -1, -1, 0, edits);
         if (editPtr != NULL) *editPtr = edits;
         return topNode;
      }
      return &(topNode->subcl[cl->subclIdx]);
   } else if (cl->par->level == topNode->cl->level && cl->par == topNode->cl) {
      if (tmlFactFile == NULL) printf("Nothing done. Class already blocked from previous evidence.\n");
      return NULL; // Redundant warning
   } else obj = blockClassForNodeRecHelper(kb, editPtr, name, topNode, cl->par, finecl, tmlFactFile, linenum);

   if (obj == NULL) return NULL; // block was found to be redundant. Nothing to be done.
   if (obj->name != NULL) {
      if (finecl->par == obj->cl) {
         if (obj->subclMask == NULL) {
            obj->subclMask = (int*)malloc(sizeof(int)*cl->nsubcls);
            subclMask = obj->subclMask;
            for (i = 0; i < obj->cl->nsubcls; i++) {
               *subclMask = 1;
               subclMask++;
            }
         }
         if (tmlFactFile == NULL) obj->subclMask[finecl->subclIdx] = 0;
         else if (obj->subclMask[i] == 1) obj->subclMask[finecl->subclIdx] = -1;
         else {
            printf("Nothing done. Class already blocked from previous evidence.\n");
            return NULL;
         }
         j = 0;
         for (i = 0; i < obj->cl->nsubcls; i++) {
            if (obj->subclMask[i] == 1) j = 1;
         }
         if (j == 0) {
            if (tmlFactFile != NULL) {
               printf("Error on line %d in fact file: Blocked classes block all subclasses of class %s.\n",
                 linenum, obj->cl->name);
               return NULL;
            } else {
               printf("Nothing done. Blocked classes block all subclasses of class %s.\n",
                  topNode->cl->name);
               obj->subclMask[finecl->subclIdx] = 1;
               return NULL;
            }
         }
         if (editPtr == NULL)
            edits = addKBEdit(obj, finecl->subclIdx, NULL, -1, -1, 0, edits);
         if (editPtr != NULL) *editPtr = edits;
         return obj;
      }
      if (editPtr != NULL) *editPtr = edits;
      return &(obj->subcl[cl->subclIdx]);
   } 

   if (cl != finecl) {
      obj->cl = cl;
      if (strchr(name, '.') == NULL)
         obj->name = name;
      obj->changed = 1;
      i = 0;
      HASH_ITER(hh, cl->part, part, tmppart) {
         obj->part[i] = (Node**)malloc(sizeof(Node*)*part->n);
         for (j = 0; j < part->n; j++)
            obj->part[i][j] = NULL;
         i++;
      }
      obj->relValues = (int**)malloc(sizeof(int*)*cl->nrels);
      i = 0;
      HASH_ITER(hh, cl->rel, rel, tmprel) {
         obj->relValues[i] = (int*)malloc(sizeof(int)*3);
         if (rel->hard == 0) {
            obj->relValues[i][0] = 0;
            obj->relValues[i][1] = 0;
            obj->relValues[i][2] = rel->numposs;
         } else if (rel->hard == 1) {
            obj->relValues[i][0] = 0;
            obj->relValues[i][1] = rel->numposs;
            obj->relValues[i][2] = 0;
         } else {
            obj->relValues[i][0] = rel->numposs;
            obj->relValues[i][1] = 0;
            obj->relValues[i][2] = 0;
         }
         i++;
      }
      obj->assignedAttr = (TMLAttrValue**)malloc(sizeof(TMLAttrValue**)*cl->nattr);  
      for (i = 0; i < cl->nattr; i++)
         obj->assignedAttr[i] = NULL;
      obj->attrValues = (int**)malloc(sizeof(int*)*cl->nattr);  
      for (i = 0; i < cl->nattr; i++)                        
         obj->attrValues[i] = NULL;
      if (obj->subcl == NULL) {
         obj->subcl = (Node*)malloc(obj->cl->nsubcls*sizeof(Node));
         for (i = 0; i < cl->nsubcls; i++) {
            obj->subcl[i].par = NULL;
            addParent(&(obj->subcl[i]), obj);
            obj->subcl[i].changed = 1;
            obj->subcl[i].name = NULL;
         }
      }
      if (obj->subclMask == NULL) {
         obj->subclMask = (int*)malloc(sizeof(int)*cl->nsubcls);
         subclMask = obj->subclMask;
         for (i = 0; i < topNode->cl->nsubcls; i++) {
            *subclMask = 1;
            subclMask++;
         }
      }
      for (i = 0; i < cl->nsubcls; i++) {
         if (cl->subcl[i] == finecl) {
            obj->subclMask[i] = 0;
         } else
            obj->subclMask[i] = 1;
      }
      obj->assignedSubcl = -1;
      if (editPtr != NULL) *editPtr = edits;
      return &(obj->subcl[cl->subclIdx]);
   }
      if (editPtr != NULL) *editPtr = edits;
     return obj;
}

/**
 * Updates the SPN to reflect new knowledge about an object's class.
 * If new information is redundant, method returns the node defined
 * by the coarsest type information for that object.
 *
 * @param kb            TML KB
 * @param name          name of the object
 * @param node          node for the object with the coarsest class information
 * @param cl            class introduced in a fact Is(name,cl)
 * @param tmlFactFile   TML fact file (needed to close if there is an error)
 * @param linenum       current line number in the fact file (used for errors)
 * @return node for this object with the coarsest type information
 */
Node* updateClassForNode(TMLKB* kb, KBEdit** editPtr, char* name, Node* node, TMLClass* cl, FILE* tmlFactFile, int linenum) {
   int i;
   TMLClass* parcl = cl->par;
   Node* initNode = node;

   if (node->cl->level == cl->level) { 
      if (node->cl != cl) {
         if (tmlFactFile != NULL) {
            printf("Error on line %d in fact file: Object %s has been assigned two mismatching classes: %s and %s.\n", linenum, name, node->cl->name, cl->name);
            freeTMLKB(kb);
            fclose(tmlFactFile); 
            exit(1);
         }
         printf("Object %s is of class %s which conflicts with new class information.\n", name, node->cl->name);
         return NULL;
      }
      if (tmlFactFile == NULL) {
         printf("Information is redundant. Object %s has already been assigned class %s.\n", name, cl->name);
         return NULL;
      }
      return initNode;  // Redundant information
   }
   while (node->assignedSubcl != -1) {
      if (node->subclMask == NULL)
         node = node->subcl;
      else
         node = &(node->subcl[node->assignedSubcl]);
      if (node->cl->level == cl->level) {
         if (node->cl != cl) {
            if (tmlFactFile != NULL) {
               printf("Error on line %d in fact file: Object %s has been assigned two mismatching classes: %s and %s.\n", linenum, name, node->cl->name, cl->name);
               return NULL;
            }
            printf("Object %s is of class %s which conflicts with new class information.\n", name, node->cl->name);
            return NULL;
         }
         if (tmlFactFile == NULL) {
            printf("Information is redundant. Object %s has already been assigned class %s.\n", name, cl->name);
            return NULL;
         }
         return initNode;  // Redundant information
      }
   }
   if (node->cl == cl) {
      if (tmlFactFile == NULL) {
         printf("Information is redundant. Object %s has already been assigned class %s.\n", name, cl->name);
         return NULL;
      }
      return initNode;
   }
   initNode = node;
   node = updateClassForNodeRecHelper(kb, editPtr, name, node, cl, cl, tmlFactFile, linenum);
   if (node == NULL) return NULL;
//   while (initNode->cl->par != NULL) initNode = *(initNode->par);
   return initNode;
}

/**
 * Updates the SPN to reflect new knowledge about a class he object is not.
 * If new information is redundant, method returns the node defined
 * by the coarsest type information for that object.
 *
 * @param kb            TML KB
 * @param name          name of the object
 * @param node          node for the object with the coarsest class information
 * @param cl            class introduced in a fact !Is(name,cl)
 * @param tmlFactFile   TML fact file (needed to close if there is an error)
 * @param linenum       current line number in the fact file (used for errors)
 * @return node for this object with the coarsest type information
 */
Node* blockClassForNode(TMLKB* kb, KBEdit** editPtr, char* name, Node* node, TMLClass* cl, FILE* tmlFactFile, int linenum) {
   int i;
   TMLClass* parcl = cl->par;
   Node* initNode = node;
   if (node->cl->level == cl->level) { 
      if (node->cl == cl) {
         if (tmlFactFile != NULL) {
            printf("Error on line %d in fact file: Object %s has previously been assigned class %s.\n", linenum, name, cl->name);
            return NULL;
         }
         printf("Object %s has previously been assigned class %s.\n", name, cl->name);
         return NULL;
      }
      if (tmlFactFile == NULL) {
         printf("Information is redundant. Object %s has already been assigned to the mutually exclusive class %s.\n", name, node->cl->name);
         return NULL;
      }
      printf("Warning on line %d in fact file: Object %s has already been assigned to the mutually exclusive class %s.\n", linenum, name, node->cl->name);
      return initNode;  // Redundant information
   }
   while (node->assignedSubcl != -1) {
      if (node->subclMask == NULL)
         node = node->subcl;
      else
         node = &(node->subcl[node->assignedSubcl]);
      if (node->cl->level == cl->level) {
         if (node->cl == cl) {
            if (tmlFactFile != NULL) {
               printf("Error on line %d in fact file: Object %s has previously been assigned class %s.\n", linenum, name, cl->name);
               return NULL;
            }
            printf("Object %s has previously been assigned class %s.\n", name, cl->name);
            return NULL;
         } else {
            if (tmlFactFile == NULL) {
               printf("Information is redundant. Object %s has already been assigned to the mutually exclusive class %s.\n", name, node->cl->name);
               return NULL;
            }
            printf("Warning on line %d in fact file: Object %s has already been assigned to the mutually exclusive class %s.\n", linenum, name, node->cl->name);
            return initNode;  // Redundant information
         }
      }
   }
   if (node->cl == cl) {
      if (tmlFactFile != NULL) {
         printf("Error on line %d in fact file: Object %s has previously been assigned class %s.\n", linenum, name, cl->name);
         return NULL;
      }
      printf("Object %s has previously been assigned class %s.\n", name, cl->name);
      return NULL;
   }
   node = blockClassForNodeRecHelper(kb, editPtr, name, node, cl, cl, tmlFactFile, linenum);
   return node;
}

/**
 * Adds nodes for an anonymous subpart object of topNode to the SPN.
 * (Fills out the SPN.)
 *
 * @param cl         current class we are creating a node for
 * @apram name       name of the object
 * @param finecl     finest type information for the subpart
 * @return a pointer to the node to set up or in the outer recursive call
 *   returns the node with the coarsest type information for the object
 */
Node* initAnonNodeToClass(TMLClass* cl, char* name, TMLClass* finecl) {
   int i, j;
   Node* ret = NULL;
   Node* obj;
   TMLPart* part;
   TMLPart* tmppart;
   TMLRelation* rel;
   TMLRelation* tmprel;
   if (cl->par != NULL) obj = initAnonNodeToClass(cl->par, name, finecl);
   else {
      obj = (Node*)malloc(sizeof(Node));
      obj->par = NULL;
   }

   obj->cl = cl;
   obj->name = NULL;
   if (cl->par == NULL)
      obj->pathname = strdup(name);
   else
      obj->pathname = (*obj->par)->name;

   obj->changed = 1;
   i = 0;
   obj->part = (Node***)malloc(sizeof(Node**)*cl->nparts);
   HASH_ITER(hh, cl->part, part, tmppart) {
      obj->part[i] = (Node**)malloc(sizeof(Node*)*part->n);
      for (j = 0; j < part->n; j++)
         obj->part[i][j] = NULL;
      i++;
   }
   i = 0;
   obj->relValues = (int**)malloc(sizeof(int*)*cl->nrels);
   HASH_ITER(hh, cl->rel, rel, tmprel) {
      obj->relValues[i] = (int*)malloc(sizeof(int)*3);
      if (rel->hard == 0) {
         obj->relValues[i][0] = 0;
         obj->relValues[i][1] = 0;
         obj->relValues[i][2] = rel->numposs;
      } else if (rel->hard == 1) {
         obj->relValues[i][0] = 0;
         obj->relValues[i][1] = rel->numposs;
         obj->relValues[i][2] = 0;
      } else {
         obj->relValues[i][0] = rel->numposs;
         obj->relValues[i][1] = 0;
         obj->relValues[i][2] = 0;
      }
      i++;
   }
   obj->assignedAttr = (TMLAttrValue**)malloc(sizeof(TMLAttrValue**)*cl->nattr);  
   for (i = 0; i < cl->nattr; i++)
      obj->assignedAttr[i] = NULL;
   obj->attrValues = (int**)malloc(sizeof(int*)*cl->nattr);  
   for (i = 0; i < cl->nattr; i++)                        
      obj->attrValues[i] = NULL;
   if (cl == finecl) {
//      if (cl->nsubcls == 0)
         obj->subcl = NULL;
/*      else {
         obj->subcl = (Node*)malloc(sizeof(Node)*cl->nsubcls);
      }
      for (i = 0; i < cl->nsubcls; i++) {
         obj->subcl[i].cl = NULL;
         obj->subcl[i].name = NULL;
         obj->subcl[i].par = NULL;
         addParent(&(obj->subcl[i]), obj);
         obj->subcl[i].changed = 1;
      }
*/
      obj->assignedSubcl = -1;
      obj->subclMask = NULL;
   } else {
      obj->subcl = (Node*)malloc(sizeof(Node));
      obj->subcl->par = NULL;
      addParent(obj->subcl, obj);
      obj->subcl->name = NULL;
      obj->subcl->changed = 1;
      ret = obj->subcl;
      obj->assignedSubcl = -1;
      obj->subclMask = NULL;
   }
   if (obj->par != NULL) {
      for (i = 0; i < (*(obj->par))->cl->nsubcls; i++) {
         if ((*(obj->par))->cl->subcl[i] == cl) {
            (*(obj->par))->assignedSubcl = i;
            break;
         }
      }
   }

   if (ret == NULL) {
      ret = obj;
      while (ret->cl->par != NULL) ret = *(ret->par);
   }
   return ret;
}

/**
 * Updates the SPN to reflect an instance fact Is(name,cl)
 * 
 * @param kb      TML KB
 * @param name    name of the object
 * @param cl      used in the recursive call, sets up a node with this class
 * @param finecl  class the object is
 * @return a pointer to the node to set up or in the outer recursive call
 *   returns the node with the coarsest type information for the object
 */
Node* initNodeToClass(TMLKB* kb, char* name, TMLClass* cl, TMLClass* finecl) {
   int i, j;
   Node* ret = NULL;
   Node* obj;
   TMLPart* part;
   TMLPart* tmppart;
   TMLRelation* rel;
   TMLRelation* tmprel;
   if (cl->par != NULL) obj = initNodeToClass(kb, name, cl->par, cl);
   else {
      obj = (Node*)malloc(sizeof(Node)); //root class
      obj->par = NULL;
      obj->npars = 0;
      if (strchr(name, '.') == NULL)
         obj->name = name;
      obj->active = 0;
      HASH_ADD_KEYPTR(hh, kb->objectNameToPtr, obj->name, strlen(obj->name), obj);
   }

   obj->cl = cl;
   if (cl->par != NULL) (*(obj->par))->assignedSubcl = cl->subclIdx;
   if (strchr(name, '.') == NULL)
      obj->name = name;
   obj->changed = 1;
   obj->active = 0;
   i = 0;
   obj->part = (Node***)malloc(sizeof(Node**)*cl->nparts);
   HASH_ITER(hh, cl->part, part, tmppart) {
      obj->part[i] = (Node**)malloc(sizeof(Node*)*part->n);
      for (j = 0; j < part->n; j++)
         obj->part[i][j] = NULL;
      i++;
   }
   i = 0;
   obj->relValues = (int**)malloc(sizeof(int*)*cl->nrels);
   HASH_ITER(hh, cl->rel, rel, tmprel) {
      obj->relValues[i] = (int*)malloc(sizeof(int)*3);
      if (rel->hard == 0) {
         obj->relValues[i][0] = 0;
         obj->relValues[i][1] = 0;
         obj->relValues[i][2] = rel->numposs;
      } else if (rel->hard == 1) {
         obj->relValues[i][0] = 0;
         obj->relValues[i][1] = rel->numposs;
         obj->relValues[i][2] = 0;
      } else {
         obj->relValues[i][0] = rel->numposs;
         obj->relValues[i][1] = 0;
         obj->relValues[i][2] = 0;
      }
      i++;
   }
   obj->assignedAttr = (TMLAttrValue**)malloc(sizeof(TMLAttrValue**)*cl->nattr);  
   for (i = 0; i < cl->nattr; i++)
      obj->assignedAttr[i] = NULL;
   obj->attrValues = (int**)malloc(sizeof(int*)*cl->nattr);  
   for (i = 0; i < cl->nattr; i++)                        
      obj->attrValues[i] = NULL;
   if (cl == finecl) {
      obj->subcl = NULL;
      obj->assignedSubcl = -1;
      obj->subclMask = NULL;
   } else {
      obj->subcl = (Node*)malloc(sizeof(Node));
      obj->subcl->par = NULL;
      addParent(obj->subcl, obj);
      obj->subcl->name = NULL;
      obj->subcl->changed = 1;
      obj->subcl->active = 0;
      ret = obj->subcl;
      obj->assignedSubcl = -1;
      obj->subclMask = NULL;
   }
   if (ret == NULL) {
      ret = obj;
      while (ret->cl->par != NULL) ret = *(ret->par);
   }
   return ret;
}

/**
 * Recursive helper function for addAndInitSubpart() -- see next function
 */
void addAndInitSubpartRecHelper(TMLKB* kb, Node* par, Node* obj, Node* subpart, char* part, int n, FILE* tmlFactFile, int linenum) {
   int i, j, c;
   TMLPart* foundPart;
   TMLClass* cl = obj->cl;
   TMLPart* piter;
   TMLPart* tmp;
   Node* subclNode;
   char anonArr[MAX_LINE_LENGTH+1];

   foundPart = getPart(cl, part);
   if (foundPart == NULL) { //recurse
      for (c = 0; c < cl->nsubcls; c++) {
         if (obj->subclMask != NULL && obj->subclMask[c] != 1) continue;
         subclNode = &(obj->subcl[c]);
         if (obj->assignedSubcl == -1 && obj->subclMask == NULL) {
            initializeNode(subclNode, cl->subcl[c], obj->name);
            addParent(subclNode, obj);
            subclNode->pathname = strdup(obj->pathname);
         }
         addAndInitSubpartRecHelper(kb, obj, subclNode, subpart, part, n, tmlFactFile, linenum);
      }
   } else {
      if (foundPart->n < (n+1)) {
         printf("Warning on line %d: Part %s.%d blocks class %s.\n", linenum, part, (n+1), cl->name);
         blockClassForNode(kb, NULL, par->name, par, cl, tmlFactFile, linenum);
         return;
      }
      i = foundPart->idx;
      obj->part[foundPart->idx][n] = subpart;
      if (subpart->pathname == NULL) {
         if (obj->pathname == NULL) {
            sprintf(anonArr, "%s.%s[%d]", obj->name, foundPart->name, (j+1));
         } else {
            sprintf(anonArr, "%s.%s[%d]", obj->pathname, foundPart->name, (j+1));
         }
         subpart->pathname = strdup(anonArr);
      }
      markNodeAncestorsAsActive(subpart);
      if (foundPart->defaultPart == 1) {
         if (obj->subcl == NULL) {
            obj->subcl = (Node*)malloc(sizeof(Node)*cl->nsubcls);
            obj->subclMask = (int*)malloc(sizeof(int)*cl->nsubcls);
            for (j = 0; j < cl->nsubcls; j++) {
               obj->subclMask[j] = 1;
               subclNode = &(obj->subcl[j]);
               initializeNode(subclNode, cl->subcl[j], obj->name);
               addParent(subclNode, obj);
               subclNode->pathname = strdup(obj->pathname);
            }
         }
         for (c = 0; c < cl->nsubcls; c++) {
            if (obj->subclMask != NULL && obj->subclMask[c] != 1) continue;
            subclNode = &(obj->subcl[c]);
            addAndInitSubpartRecHelper(kb, obj, subclNode, subpart, part, n, tmlFactFile, linenum);
         }
      }
   }
}
/**
 * Update the SPN to reflect a new subpart fact Has(name,subpartname,part)
 *
 * @param kb            TML KB
 * @param name          name of the object
 * @param subpartname   name of the subpart object
 * @param obj           node assigned to the object name and its coarsest type information
 * @param part          name of the subpart relation
 * @param n             index of the part
 * @param tmlFactFile   TML fact file (needed to close if an error occurs)
 * @param linenum       current line number in the fact file (used for errors)
 * @return node for the subpart object with its coarest type information
 */
Node* addAndInitSubpart(TMLKB* kb, char* name, char* subpartname, Node* obj, char* part, int n, FILE* tmlFactFile, int linenum) {
   TMLClass* partcl;
   int i, j, k;
   int found = 0;
   TMLPart* foundPart;
   TMLPart* lowestPart;
   TMLClass* cl = obj->cl;
   Node* node;
   TMLPart* tmp;
   TMLPart* piter;
   Node* subclNode;
   char anonArr[MAX_LINE_LENGTH+1];
   Node* tmpNode;

   foundPart = NULL;
   do {
      tmp = getPart(cl, part);
      if (tmp != NULL) {
         foundPart = tmp;
         if (tmp->n < n)
            break;
      }
      if (cl->nsubcls == 0 || obj->assignedSubcl == -1) break;
      if (obj->subclMask == NULL)
         obj = obj->subcl;
      else
         obj = &(obj->subcl[obj->assignedSubcl]);
      cl = obj->cl;
   } while (TRUE);

   if (foundPart == NULL) {
      printf("Error on line %d in fact file: Part name %s could not be found in known classes for object %s.\n", linenum, part, name);
      return NULL;
   }
   partcl = foundPart->cl;
   if (foundPart->maxNumParts < n) {
      printf("Error on line %d in fact file: Exceeded the number of parts of name %s for object %s.\n", linenum, part, name);
      return NULL;
   }
   n--;
   i = foundPart->idx;

   if (obj->part[i][n] != NULL) {
      n++;
      if (n % 10 == 3)
        printf("Error on line %d in fact file: The 3rd part %s of object %s has already been declared.\n", linenum, part, name);
      else if (n % 10 == 1)
        printf("Error on line %d in fact file: The 1st part %s of object %s has already been declared.\n", linenum, part, name);
      else if (n % 10 == 2)
        printf("Error on line %d in fact file: The 2nd part %s of object %s has already been declared.\n", linenum, part, name);
      else
        printf("Error on line %d in fact file: The %dth part %s of object %s has already been declared.\n", linenum, n, part, name);
      return NULL;
   }
   node = initNodeToClass(kb, subpartname, partcl, partcl);
   obj->part[i][n] = node;
   addParent(node, obj);
   if (obj->pathname == NULL) {
      sprintf(anonArr, "%s.%s[%d]", obj->name, foundPart->name, (n+1));
   } else {
      sprintf(anonArr, "%s.%s[%d]", obj->pathname, foundPart->name, (n+1));
   }
   node->pathname = strdup(anonArr);
   if (cl->nsubcls != 0 && obj->assignedSubcl != -1 && foundPart->defaultPart == 1) {
      obj = obj->subcl;
      cl = obj->cl;
      lowestPart = foundPart;
      while (TRUE) {
         foundPart = getPart(cl, part);
         if (foundPart == NULL) {
            if (cl->nsubcls == 0 || obj->assignedSubcl == -1) break;
            obj = obj->subcl;
            cl = obj->cl;
            continue;
         }
         lowestPart = foundPart;
         if (lowestPart->n < (n+1)) {
            printf("Error on line %d in fact file: Exceeded the number of parts of name %s for object %s.\n", linenum, part, name);
            return NULL;
         }
         i = lowestPart->idx;
         obj->part[i][n] = node;
         if (cl->nsubcls == 0 || obj->assignedSubcl == -1 || foundPart->defaultPart == 0) break;
         obj = obj->subcl;
         cl = obj->cl;
      }
      foundPart = lowestPart;
   }

   tmpNode = obj;
   while (tmpNode->cl->par != NULL) {
      tmpNode = *(tmpNode->par);
      cl = tmpNode->cl;
      tmp = getPart(cl, part);
      if (tmp != NULL) {
         tmpNode->part[tmp->idx][n] = node;
      }
   }

   if (foundPart->defaultPart == 1) {
      if (obj->subcl == NULL) {
         obj->subcl = (Node*)malloc(sizeof(Node)*obj->cl->nsubcls);
         obj->subclMask = (int*)malloc(sizeof(int)*obj->cl->nsubcls);
         for (j = 0; j < obj->cl->nsubcls; j++) {
            obj->subclMask[j] = 1;
            subclNode = &(obj->subcl[j]);
            subclNode->name = NULL;
            initializeNode(subclNode, cl->subcl[j], obj->name);
            addParent(subclNode, obj);
            subclNode->pathname = strdup(obj->pathname);
         }
      }
      for (j = 0; j < cl->nsubcls; j++) {
         if (foundPart->defaultPartForSubcl[j] == 1) {
            if (obj->subclMask != NULL && obj->subclMask[j] != 1) continue;
            subclNode = &(obj->subcl[j]);
            addAndInitSubpartRecHelper(kb, obj, subclNode, node, part, n, tmlFactFile, linenum);
         }
      }
   }

   return node;
}

/**
 * Checks to see if a class or a descendant of that class is
 * a part of some other class. This check is used to make sure
 * that there is only one class (the top class) that is not
 * a subpart (or has a descendant that is a subpart) of any other
 * class.
 *
 * TODO: We could decide to change the error that results from
 * a lost root into a warning.
 */
int checkForLostRoot(TMLClass* cl) {
   int c;
   int found;
   if (cl->isPart == 1) return 1;
   if (cl->nsubcls == 0) return 0;
   for (c = 0; c < cl->nsubcls; c++) {
      found = checkForLostRoot(cl->subcl[c]);
      if (found == 1) return 1;
   }
   return 0;
}

char* getLineToSemicolon(FILE* file, char** prev, int* linenum) {
   char line[MAX_LINE_LENGTH+1];
   char nocomments[MAX_LINE_LENGTH+1];
   char endline[3];
   char* out = NULL;
   char* tmp;
   int len1;
   int len2;
   char* semicolon;
   char* comment;
   int hasComment = 0;
   int endType;
   char* curr = *prev;
   do {
      if (curr == NULL) {
         if (fgets(line, MAX_LINE_LENGTH, file) == NULL) {
            if (*prev != NULL) free(*prev);
            if (out != NULL) free(out);
            return NULL;
         }
         curr = line;
      }
      (*linenum)++;
      if (sscanf(curr, "%1s", endline) < 1) {
         curr = NULL;
         continue; 
      }
      hasComment = 0;
      comment = strchr(curr, '/');
      if (comment != NULL) {
         if (comment+1 == NULL || *(comment+1) != '/') {
            printf("Unexpected \'/\' on line %d.\n", *linenum);
            if (*prev != NULL) free(*prev);
            if (out != NULL) return out;
            return NULL;
         }
         hasComment = 1;
         *comment = '\n';
         *(comment+1) = '\0';
      }
      semicolon = strchr(curr, ';');
      if (semicolon == NULL) {
         semicolon = strchr(curr, '{');
         if (semicolon == NULL) {
            semicolon = strchr(curr, '}');
            if (semicolon != NULL) endType = 3;
         } else endType = 2;
      } else endType = 1;

      if (semicolon != NULL) {
         *semicolon = '\0';
         if (semicolon+1 != NULL && sscanf(semicolon+1, "%1s", endline) >= 1) {
            if (*prev != NULL) free(*prev);
            if (strcmp(endline, ";") == 0) {
               printf("Unexpected \';\' on line %d.\n", *linenum);
               return NULL;
            }
            *prev = strdup(semicolon+1);
         } else if (*prev != NULL) free(*prev);

         if (out == NULL) {
            len1 = strlen(curr);
            if (endType == 1) {
               out = strdup(curr);
               *semicolon = ';';
            }
            else if (endType == 2) {
               out = (char*)malloc(len1+2);
               memcpy(out, curr, len1);
               out[len1] = '{';
               *semicolon = '{';
               out[len1+1] = '\0';
            } else {
               out = (char*)malloc(len1+2);
               memcpy(out, curr, len1);
               out[len1] = '}';
               *semicolon = '}';
               out[len1+1] = '\0';
            }
         } else {
            len1 = strlen(out);
            len2 = strlen(curr);
            if (endType == 1) {
               tmp = (char*)malloc(len1 + len2 + 2);
               memcpy(tmp, out, len1);
               tmp[len1] = ' ';
               memcpy(tmp+len1+1, curr, len2);
               *semicolon = ';';
               tmp[len1+len2+1] = '\0';
            } else if (endType == 2) {
               tmp = (char*)malloc(len1 + len2 + 3);
               memcpy(tmp, out, len1);
               tmp[len1] = ' ';
               memcpy(tmp+len1+1, curr, len2);
               tmp[len1+len2+1] = '{';
               *semicolon = '{';
               tmp[len1+len2+2] = '\0';
            } else {
               tmp = (char*)malloc(len1 + len2 + 3);
               memcpy(tmp, out, len1);
               tmp[len1] = ' ';
               memcpy(tmp+len1+1, curr, len2);
               tmp[len1+len2+1] = '}';
               *semicolon = '}';
               tmp[len1+len2+2] = '\0';
            }
            free(out);
            out = tmp;
         }
         if (hasComment == 1) {
            *comment = '/';
            *(comment+1) = '/';
         }
         break;
      }
      
      if (out == NULL) {
         out = strdup(curr);
         len1 = strlen(curr);
         out = (char*)malloc(len1);
         memcpy(out, curr, len1);
         out[len1-1] = '\0';
      } else {
         len1 = strlen(out);
         len2 = strlen(curr);
         tmp = (char*)malloc(len1 + len2 + 1);
         memcpy(tmp, out, len1);
         tmp[len1] = ' ';
         memcpy(tmp+len1+1, curr, len2-1);
         tmp[len1+len2] = '\0';
         free(out);
         out = tmp;
      }
      if (hasComment == 1) {
         *comment = '/';
         *(comment+1) = '/';
      }
      curr = NULL;
   } while (TRUE);
   return out;
}

/**
 * Read in subclasses line in an object description and add
 * that information to the object nodes
 */
int readInObjectClasses(TMLKB* kb, Node* node, FILE* tmlFactFile, int linenum, char* line) {
   char* iter = line;
   int n = 0;
   char fmt_str[50];
   char comma[2];
   char className[MAX_NAME_LENGTH+1];
   int i;
   int correctScan;
   Node* output;

   while (TRUE) {
      iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   snprintf(fmt_str, 50, " %%%d[!a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);
   iter = line;
   for (i = 0; i < n; i++) {
      correctScan = sscanf(iter, fmt_str, className, comma);
      if ((i == n-1 && correctScan != 1) || (i != n-1 && correctScan != 2) || (i != n-1 && correctScan == 2 && comma[0] != ',')) {
         printf("Error in subclass description for object %s.\n", (node->name == NULL ? node->pathname : node->name));
         return 0;
      }
      if (className[0] == '!') {
         output = addClassEvidenceForObj(kb, (node->name == NULL ? node->pathname : node->name), node, className+1, 0, tmlFactFile, linenum);
      } else {
         output = addClassEvidenceForObj(kb, (node->name == NULL ? node->pathname : node->name), node, className, 1, tmlFactFile, linenum);
      }
      if (output == NULL) return 0;
      iter = strchr(iter, ',');
      if (iter != NULL)
         iter++;
   }
   return 1;
}

/**
 * Read in subparts line in an object description and add
 * that information to the object nodes
 */
int readInObjectSubparts(TMLKB* kb, Node* node, char* bestName, FILE* tmlFactFile, int linenum, char* line) {
   char* iter = line;
   int n = 0;
   char fmt_str[50];
   char fmt2_str[60];
   char comma[2];
   char partName[MAX_NAME_LENGTH+1];
   char partRel[MAX_NAME_LENGTH+1];
   int partIdx = -1;
   int i, j;
   int correctScan;
   Node* subpartNode;
   char anonArr[MAX_LINE_LENGTH+1];
   TMLPart* part;
   Node* tmpnode;
   TMLClass* tmpcl;
   TMLPart* piter;
   TMLPart* tmp;
   Node* output;
   Name_and_Ptr* clList;
   QNode* qcurr;
   QNode* qprev;

   while (TRUE) {
      iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   snprintf(fmt_str, 50, " %%%d[a-zA-Z0-9:] %%%d[a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(fmt2_str, 60, " %%%d[a-zA-Z0-9:][%%d] %%%d[a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   iter = line;
   for (i = 0; i < n; i++) {
      correctScan = sscanf(iter, fmt_str, partRel, partName, comma);
      if ((i == n-1 && correctScan != 2) || (i != n-1 && correctScan != 3)
            || (i != n-1 && correctScan == 3 && comma[0] != ',')) {
         correctScan = sscanf(iter, fmt2_str, partRel, &partIdx, partName, comma);
         if ((i == n-1 && correctScan != 3) || (i != n-1 && correctScan != 4)
            || (i != n-1 && correctScan == 4 && comma[0] != ',')) {
            printf("Error in subpart description of object %s.\n", bestName);
            return 0;
         }
      } else partIdx = 1;
      if (partIdx <= 0) {
         printf("Error in subpart description for object %s. Part index %d of %s is not positive.\n", bestName, partIdx, partRel);
         return 0;
      }
      
      HASH_FIND_STR(kb->objectNameToPtr, partName, subpartNode);      
      if (subpartNode == NULL) {
         output = addAndInitSubpart(kb, bestName, strdup(partName), node, partRel, partIdx, tmlFactFile, linenum);
         if (output == NULL) return 0;
      } else {
         part = NULL;
         tmpcl = node->cl;
         tmpnode = node;
         do {
            part = getPart(tmpcl, partRel);
            if (part != NULL) break;
            if (tmpcl->nsubcls == 0 || tmpnode->assignedSubcl == -1) break;
            if (tmpnode->subclMask != NULL) tmpnode = &(tmpnode->subcl[tmpnode->assignedSubcl]);
            else tmpnode = tmpnode->subcl;
            tmpcl = tmpnode->cl;
         } while (TRUE);

         if (part == NULL) {
            printf("Error in subpart description for object %s. Part name %s could not be found in known classes for object %s.\n", bestName, partRel, bestName);
            return 0;
         }
         if (part->n < partIdx) {
            printf("Error in subpart description for object %s. Exceeded the number of parts of name %s for object %s.\n", bestName, partRel, bestName);
            return 0;
         }
         j = 0;
         HASH_ITER(hh, tmpcl->part, piter, tmp) {
            if (strcmp(piter->name, partRel) == 0) break;
            j++;
         }

         if (tmpnode->part[j][partIdx-1] != NULL) {
            if (partIdx % 10 == 3)
              printf("Error in subpart description for object %s. The 3rd part %s of object %s has already been declared.\n", bestName, partRel, bestName);
            else if (partIdx % 10 == 1)
              printf("Error in subpart description for object %s. The 1st part %s of object %s has already been declared.\n", bestName, partRel, bestName);
            else if (partIdx % 10 == 2)
              printf("Error in subpart description for object %s. The 2nd part %s of object %s has already been declared.\n", bestName, partRel, bestName);
            else
              printf("Error in subpart description for object %s. The %dth part %s of object %s has already been declared.\n", bestName, partIdx, partRel, bestName);
         }
         tmpnode->part[j][partIdx-1] = subpartNode;
         if (subpartNode->pathname == NULL) {
            if (node->pathname == NULL) {
               sprintf(anonArr, "%s.%s[%d]", node->name, part->name, partIdx);
            } else {
               sprintf(anonArr, "%s.%s[%d]", node->pathname, part->name, partIdx);
            }
            subpartNode->pathname = strdup(anonArr);
            printf("%s %s\n", bestName, subpartNode->pathname);
         } else {
            printf("Error in fact file: Currently, Alchemy Lite does not allow an object to be a subpart of more than one object in a given world. (%s is in this file.)\n", partName);
            return 0;
         }
         markNodeAncestorsAsActive(subpartNode);
      }
      HASH_FIND_STR(kb->objectNameToPtr, partName, subpartNode);
      subpartNode->active = 1;
      markNodeAncestorsAsActive(subpartNode);
      iter = strchr(iter, ',');
      if (iter != NULL)
         iter++;
   }
   if (iter != NULL) {
      correctScan = sscanf(iter, "%1s", comma);
      if (correctScan == 1) {
         printf("Error in subpart description for object %s. Unexpected expression \"%s\" at the end of the subpart line.\n", bestName, iter);
         return 0;
      }
   }
   return 1;
}

/**
 * Read in relations line in an object description and add
 * that information to the object nodes
 */
int readInObjectRelations(TMLKB* kb, Node* node, char* bestName, FILE* tmlFactFile, int linenum, char* line) {
   char* iter = line;
   char rel_fmt_str[60];
   char rel_noargs_fmt_str[50];
   char rel_noargs2_fmt_str[50];
   char comma[2];
   char rparen[2];
   char argsStr[MAX_LINE_LENGTH+1];
   char relName[MAX_NAME_LENGTH+1];
   char* iter2;
   int correctScan;
   int i, j, n;
   int pol;
   char* relation;
   TMLRelation* rel;
   TMLRelation* foundrel;
   int numparts;
   char* normalizedRelationStr;
   char** args;
   TMLClass* cl;
   Node* relNode;
   Node* foundrelNode;
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash;
   int p;
   char* partname;
   Node* subpartNode;
   Node* subclNode;
   int partCl;
   char* partName;
   int nump;
   TMLPart* tmppart;
   TMLPart* foundpart;
   int partIdx;

   n = 0;
   while (TRUE) {
      iter2 = strchr(iter, ')');
      if (iter2 != NULL)
         iter = strchr(iter2, ',');
      else
         iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   snprintf(rel_fmt_str, 60, " %%%d[!a-zA-Z0-9:] (%%%d[][a-zA-Z0-9:,]%%1[)] %%1s", MAX_NAME_LENGTH, MAX_LINE_LENGTH);
   snprintf(rel_noargs_fmt_str, 50, " %%%d[!a-zA-Z0-9:] ( %%1[)] %%1s", MAX_NAME_LENGTH);
   snprintf(rel_noargs2_fmt_str, 50, " %%%d[!a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);

   iter = line;
   for (i = 0; i < n; i++) {
      correctScan = sscanf(iter, rel_fmt_str, relName, argsStr, rparen, comma);
      if ((i != n-1 && correctScan != 4) || (i != n-1 && correctScan == 4 && comma[0] != ',')
         || (i == n-1 && correctScan != 3)) {
         correctScan = sscanf(iter, rel_noargs_fmt_str, relName, rparen, comma);
         if ((i != n-1 && correctScan != 3) || (i != n-1 && correctScan == 3 && comma[0] != ',')
            || (i == n-1 && correctScan != 2)) {
            correctScan = sscanf(iter, rel_noargs2_fmt_str, relName, comma);
            if ((i != n-1 && correctScan != 2) || (i != n-1 && correctScan == 2 && comma[0] != ',')
                  || (i == n-1 && correctScan != 1)) {
               printf("Error in fact file: Malformed relation line for object %s. Expected R(Obj1,...Objn), !S(), ...;\n", bestName);
               return 0;
            }
            argsStr[0] = '\0';
         } else argsStr[0] = '\0';
      }

      if (relName[0] == '!') {
         relation = relName+1;
         pol = 0;
      } else {
         relation = relName;
         pol = 1;
      }
      if (argsStr[0] == '\0')
         normalizedRelationStr = splitRelArgsAndCreateNormalizedRelStr(kb, NULL, relation, &numparts, &args, 0);
      else
         normalizedRelationStr = splitRelArgsAndCreateNormalizedRelStr(kb, argsStr, relation, &numparts, &args, 0);
      if (normalizedRelationStr == NULL) {
         printf("Error in fact file: Malformed relation fact for %s. Expected entries of the form R(Object, Part1,...,Partn)\n", bestName);
         return 0;
      }
      cl = node->cl;
      relNode = node;
      rel = getRelation(cl, relation);
      if (rel != NULL)
         foundrelNode = relNode;
      else
         foundrelNode = NULL;
      while (relNode->assignedSubcl != -1) {
         if (relNode->subclMask == NULL)
            relNode = relNode->subcl;
         else
            relNode = &(relNode->subcl[relNode->assignedSubcl]);
         cl = relNode->cl;
         foundrel = getRelation(cl, relation);
         if (foundrel != NULL) {
            rel = foundrel;
            foundrelNode = relNode;
         }
      }
      if (rel == NULL) {
         printf("Error in fact file: Relation %s not defined for object %s.\n", relation, bestName);
         return 0;
      }
      relNode = foundrelNode;
      cl = relNode->cl;
      if (rel->nargs != numparts) {
         printf("Error in fact file: Relation %s has %d parts, not %d.\n", relation, rel->nargs, numparts);
         for (j = 0; j < numparts; j++) {
            free(args[j]);
         }
         free(args);
         return 0;
      }
      if ((pol == 1 && rel->hard == -1) || (pol == 0 && rel->hard == 1)) {
         printf("Error in fact file: Relation %s for class %s is hard with a different polarity than is defined here.\n", rel->name, cl->name);
         for (j = 0; j < numparts; j++) {
            free(args[j]);
         }
         free(args);
         return 0;
      }
      HASH_FIND_STR(kb->objToRelFactStrs, node->pathname, objRelHash);
      if (objRelHash == NULL) {
         objRelHash = (ObjRelStrsHash*)malloc(sizeof(ObjRelStrsHash));
         objRelHash->obj = node->pathname;
         objRelHash->hash = NULL;
         relStrHash = (RelationStr_Hash*)malloc(sizeof(RelationStr_Hash));
         if (pol == 0) {
            relStrHash->pol = 0;
            relStrHash->str = normalizedRelationStr;
         } else {
            relStrHash->pol = 1;
            relStrHash->str = normalizedRelationStr;
         }
         HASH_ADD_KEYPTR(hh, objRelHash->hash, relStrHash->str, strlen(relStrHash->str), relStrHash);
         HASH_ADD_KEYPTR(hh, kb->objToRelFactStrs, objRelHash->obj, strlen(objRelHash->obj), objRelHash);
      } else {
         HASH_FIND_STR(objRelHash->hash, normalizedRelationStr, relStrHash);

         if (relStrHash == NULL) {
            relStrHash = (RelationStr_Hash*)malloc(sizeof(RelationStr_Hash));
            if (pol == 0) {
               relStrHash->pol = 0;
               relStrHash->str = normalizedRelationStr;
            } else {
               relStrHash->pol = 1;
               relStrHash->str = normalizedRelationStr;
            }
            HASH_ADD_KEYPTR(hh, objRelHash->hash, relStrHash->str, strlen(relStrHash->str), relStrHash);
         } else {
            free(normalizedRelationStr);
            if (relStrHash->pol != pol) {
               printf("Error in fact file: Relation fact %s(%s) for object %s has been declared both positive and negative.\n", relation, argsStr, bestName);
               for (j = 0; j < numparts; j++) {
                  free(args[j]);
               }
               free(args);
            }
         }
      }

      for (p = 0; p < numparts; p++) {
         partname = args[p];
         HASH_FIND_STR(kb->objectNameToPtr, partname, subpartNode);
         if (subpartNode == NULL)
            subpartNode = findNodeFromAnonName(kb, node, partname, 1);
         if (subpartNode == NULL) {
            printf("Error in fact file: Unknown object %s. Must be set as a subpart of %s if its in a relation.\n", partname, bestName);
            for (i = 0; i < numparts; i++) {
               free(args[p]);
            }
            free(args);
         }
         subclNode = node;
         partCl = rel->argClass[p];
         partName = rel->argPartName[p];
         if (partCl != -1) {
            while (subclNode->cl->id != partCl) {
               if (subclNode->subclMask == NULL)
                  subclNode = subclNode->subcl;
               else
                  subclNode = &(subclNode->subcl[subclNode->assignedSubcl]);
            }
            nump = getPart(subclNode->cl, partName)->n;
         } else {
            tmppart = getPart(subclNode->cl, partName);
            while (tmppart == NULL) {
               if (subclNode->subclMask == NULL)
                  subclNode = subclNode->subcl;
               else
                  subclNode = &(subclNode->subcl[subclNode->assignedSubcl]);
               tmppart = getPart(subclNode->cl, partName);
            }
            nump = tmppart->n;
         }
         partIdx = 0;
         HASH_ITER(hh, subclNode->cl->part, foundpart, tmppart) {
            if (strcmp(foundpart->name, partName) == 0) break;
            partIdx++;
         }
         for (j = 0; j < n; j++) {
            if (subclNode->part[partIdx][i] == subpartNode) break;
         }
         if (j == nump) {
            printf("Error in fact file: Object %s is not a %s part of object %s.\n",
                     args[p], subclNode->cl->part[p].name, bestName);
            for (j = 0; j < numparts; j++) {
               free(args[j]);
            }
            free(args);
         }
      }
      for (j = 0; j < numparts; j++) {
         free(args[j]);
      }
      free(args);

      addRelationToKB(NULL, relNode, relation, pol);

      iter2 = strchr(iter, ')');
      if (iter2 == NULL)
         iter = strchr(iter, ',');
      else
         iter = strchr(iter2, ',');
      if (iter != NULL) iter++;
   }

   return 1;
}

/**
 * Read in subclasses line of a class description
 * and add that information to the KB
 */
int readInSubclasses(TMLKB* kb, TMLClass* cl, char* line) {
   char* iter = line;
   int n = 0;
   int i;
   char fmt_str[50];
   char comma[2];
   char subclStr[MAX_NAME_LENGTH+1];
   float wt;
   int correctScan;
   Name_and_Ptr* name_and_ptr;
   TMLClass* subcl;
   TMLClass* tempcl;

   snprintf(fmt_str, 50, " %%%d[a-zA-Z0-9:] %%f %%1s", MAX_NAME_LENGTH);
   while (TRUE) {
      iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   cl->subcl = (TMLClass**)calloc(n, sizeof(TMLClass*));
   cl->wt = (float*)calloc(n, sizeof(float));
   cl->cnt = (int*)calloc(n, sizeof(int));
   cl->nsubcls = n;

   iter = line;
   for (i = 0; i < n; i++) {
      correctScan = sscanf(iter, fmt_str, subclStr, &wt, comma);
      if ((i == n-1 && correctScan != 2) || (i != n-1 && correctScan != 3)
         ||(i != n-1 && correctScan == 3 && comma[0] != ',')) {
         printf("Error in subclasses description of class %s. Expected \"SubCl wt, SubCl wt, ..., SubCl wt;\n", cl->name);
         return 0;
      }
      HASH_FIND_STR(kb->classNameToPtr, subclStr, subcl);
      if (subcl == NULL) {
         printf("Error in subclasses description of class %s: Subclass %s is undefined.\n", cl->name, subclStr);
         return 0;
      }
      subcl->subclIdx = i;
      if (subcl->par != NULL) {
         printf("Error in subclasses description of class %s: Class %s is already a subclass of %s.\n",cl->name, subclStr, subcl->par->name);
         return 0;
      }
      if (subcl == kb->topcl) {
         printf("Error in subclasses description of class %s: The Top Class %s cannot be a subclass of any class.\n", cl->name, subclStr);
         return 0;
      }
      subcl->par = cl;
      updateClassLevel(subcl);
      // VALIDITY CHECK: If cl1 being a subclass of cl2 introduces
      // a cycle, class hierarchy will not be a forest
      if (cl->par != NULL) {
         tempcl = cl->par;
         while (tempcl != NULL) {
            if (subcl->id == tempcl->id) {
               printf("Error in subclasses description of class %s: If %s is the subclass of %s, then the class hierarchy has a cycle.\n",
                  cl->name, subclStr, cl->name);
               return 0;
            }
            tempcl = tempcl->par;
         }
      }
      cl->subcl[i] = subcl;
      cl->wt[i] = wt;
      iter = strchr(iter, ',');
      if (iter != NULL)
          iter++; 
   }
   return 1;
}

int createsSubpartClassCycle(TMLClass* cl, TMLClass* partcl) {
   TMLClass* rootCl = rootClass(cl);
   TMLPart* part;
   TMLPart* tmp;
   int cycle = 0;
   if (rootCl == partcl) return 1;
   HASH_ITER(hh, partcl->part, part, tmp) {
      if (createsSubpartClassCycle(rootCl, part->cl) == 1)
         return 1;
   }
   if (partcl->par != NULL) {
      return createsSubpartClassCycle(cl, partcl->par);
   }
   return 0;
}

/**
 * Read in subparts line of a class description
 * and add that information to the KB
 */
int readInSubparts(TMLKB* kb, TMLClass* cl, char* line) {
   char* iter = line;
   int n = 0;
   int i, j, k;
   char fmt_str_1[50];
   char fmt_str_2[50];
   char fmt_str_3[50];
   char fmt_str_4[50];
   char comma[2];
   char subclStr[MAX_NAME_LENGTH+1];
   char partStr[MAX_NAME_LENGTH+1];
   float wt;
   int correctScan;
   TMLClass* pcl;
   TMLClass* tempcl;
   int nparts;
   int partType;
   TMLPart* part;
   char* partName;
   char* piter;
   int firstFind;
   TMLPart* foundpart;
   TMLClass* parcl;

   snprintf(fmt_str_1, 50, " %%%d[a-zA-Z0-9:][%%d] %%1s", MAX_NAME_LENGTH);
   snprintf(fmt_str_2, 50, " %%%d[a-zA-Z0-9:] %%%d[a-zA-Z0-9:][%%d] %%1s", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(fmt_str_3, 50, " %%%d[a-zA-Z0-9:] %%%d[a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(fmt_str_4, 50, " %%%d[a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);
   while (TRUE) {
      iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   cl->nparts = n;

   iter = line;
   for (i = 0; i < n; i++) {
      correctScan = sscanf(iter, fmt_str_1, subclStr, &nparts, comma);
      if ((i != n-1 && correctScan == 3 && comma[0] == ',') || (i == n-1 && correctScan == 2)) { 
         partType = 2;
         partStr[0] = '\0';
      } else {
         correctScan = sscanf(iter, fmt_str_2, subclStr, partStr, &nparts, comma);
         if ((i != n-1 && correctScan == 4 && comma[0] == ',') || (i == n-1 && correctScan == 3))
            partType = 4;
         else {
            correctScan = sscanf(iter, fmt_str_3, subclStr, partStr, comma);
            if ((i != n-1 && correctScan == 3 && comma[0] == ',') || (i == n-1 && correctScan == 2)) partType = 3;
            else {
               correctScan = sscanf(iter, fmt_str_4, subclStr, comma);
               if ((i != n-1 && correctScan == 2 && comma[0] == ',') || (i == n-1 && correctScan == 1)) {
                  partType = 1;
                  partStr[0] = '\0';
               } else {
                  printf("Malformed subpart descriptions for class %s. Expected entries of the forms (1) SubpartClass, (2) SubpartClass #n, (3) SubpartClass SubpartName, or (4) SubpartClass SubpartName #n;\n", cl->name);
                  return 0;
               }
            }
         }
      }
      HASH_FIND_STR(kb->classNameToPtr, subclStr, pcl);
      if (pcl == NULL) {
         printf("Error in subpart description for class %s: Class %s is undefined.\n", cl->name, subclStr);
         return 0;
      }
      if (pcl == kb->topcl) {
         printf("Error in subpart description for class %s: The Top Class %s cannot be a subclass of any class.\n", cl->name, subclStr);
         return 0;
      }
      if (createsSubpartClassCycle(cl, pcl) == 1) {
         printf("Error in subpart description for class %s: Parts of class %s cannot be a subpart of %s since this would cause a cycle in the subpart graph.\n", cl->name, subclStr, cl->name);
         return 0;
      }
      pcl->isPart = 1;
      part = (TMLPart*)malloc(sizeof(TMLPart));
      part->defaultPartForSubcl = NULL;
      part->defaultPart = 0;
      part->overridePart = 0;
      part->maxNumParts = 0;
      part->clOfOverriddenPart = pcl;

      if (partType == 1) {
         partName = strdup(subclStr);
         part->name = partName;
         part->n = nparts;
      } else if (partType == 2) { // Type: Subpart Class n
         partName = strdup(subclStr);
         part->name = partName;
         part->n = nparts;
         if (part->n <= 0) {
            printf("Error in subpart description for class %s: Number of parts %s must be greater or equal to 1.\n", cl->name, subclStr);
            return 0;
         }
      } else if (partType == 3) { // Type: SubpartClass PartName
         piter = strpbrk(partStr, "0123456789");
         if (piter == partStr) {
            printf("Error in subpart description for class %s: Part names must begin with a letter. %s does not.\n", cl->name, partStr);
            return 0;
         }
         partName = strdup(partStr);
         part->name = partName;
         part->n = 1;
         HASH_FIND_STR(kb->classNameToPtr, partStr, tempcl);
         if (tempcl != NULL) {
            printf("Error in subpart description for class %s: A class named %s has already been declared. It cannot be the name of a part.\n", cl->name, partStr);
            return 0;
         }
      } else if (partType == 4) { // Type: SubpartClass PartName n
         piter = strpbrk(partStr, "0123456789");
         if (piter == partStr) {
            printf("Error in subpart description for class %s: Part names must begin with a letter. %s does not.\n", cl->name, partStr);
            return 0;
         }
         partName = strdup(partStr);
         part->name = partName;
         part->n = nparts;
      }
      part->maxNumParts = nparts;
      if (getPart(cl, partName) != NULL) {
         printf("Error in subpart description for class %s: Parts with the name %s have already been declared for class %s.\n", cl->name, partName, cl->name);
         return 0;
      }
      if (cl->par != NULL) {
         tempcl = cl->par;
         j = cl->subclIdx;
         firstFind = 1;
         while (tempcl != NULL) {
            foundpart = getPart(tempcl, partName);
            if (foundpart != NULL) {
               part->overridePart = 1;
               parcl = foundpart->cl;
               if (firstFind == 1) {
                  firstFind = 0;
                  if (!isAncestor(parcl,pcl)) {
                     printf("Error in subpart description for class %s: Part %s overrides parts of the same name in an ancestor class with an incompatible class %s. \
                        When overriding parts, you must use the same class as the parts you are overriding or as subclasses of those parts.\n", cl->name, partName, pcl->name);
                     return 0;
                  }
               }
               if (foundpart->defaultPart == 0) {
                  foundpart->defaultPartForSubcl = (int*)malloc(sizeof(int)*tempcl->nsubcls);
                  for (k = 0; k < tempcl->nsubcls; k++)
                     foundpart->defaultPartForSubcl[k] = 0;
                  foundpart->defaultPart = 1;
               }
               foundpart->defaultPartForSubcl[j] = 1;
               if (foundpart->maxNumParts < part->maxNumParts) {
                  foundpart->maxNumParts = part->maxNumParts;
               }
               part->clOfOverriddenPart = parcl;
            }
//              if (foundpart != NULL && foundpart->overridePart == 0) break;
            j = tempcl->subclIdx;
            tempcl = tempcl->par;
         }
      }
      part->idx = i;
      HASH_ADD_KEYPTR(hh, cl->part, part->name, strlen(part->name), part);
      part->cl = pcl;
      iter = strchr(iter, ',');
      if (iter != NULL)
          iter++; 
   }
   return 1;
}

/**
 * Read in an attribute line of an object description
 * and add that information to the object nodes
 */
int readInObjectAttribute(TMLKB* kb, Node* node, char* bestName, char* attrName, char* line) {
   char* iter = line;
   char fmt_str[40];
   char attrValue[MAX_NAME_LENGTH+1];
   char end[2];
   int correctScan;
   char keyword_fmt_str[15];
   char value_fmt_str[20];
   char* start;
   TMLAttribute* attr;
   TMLAttrValue* attrval;
   TMLClass* tmpcl;
   Node* tmpnode;
   Node* foundNode;
   TMLAttribute* foundattr;
   int i;

   snprintf(value_fmt_str, 20, " %%%d[!a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);
   tmpcl = node->cl;
   tmpnode = node;
   attr = getAttribute(tmpcl, attrName);
   if (attr != NULL)
      foundNode = tmpnode;
   else
      foundNode = NULL;
   while (tmpnode->assignedSubcl != -1) {
      if (tmpnode->subclMask == NULL)
         tmpnode = tmpnode->subcl;
      else
         tmpnode = &(tmpnode->subcl[tmpnode->assignedSubcl]);
      tmpcl = tmpnode->cl;
      foundattr = getAttribute(tmpcl, attrName);
      if (foundattr != NULL) {
         attr = foundattr;
         foundNode = tmpnode;
      }
   }
   if (attr == NULL) {
      printf("Error in description for object %s. Unknown attribute %s.\n", bestName, attrName);
      return 0;
   }

   iter = line;
   while (iter != NULL) {
      correctScan = sscanf(iter, value_fmt_str, attrValue, end);
      if (correctScan < 1 || (correctScan == 2 && end[0] != ',')) {
         printf("Error in description for object %s. Malformed attribute line \"%s\".\n", bestName, line);
         return 0;
      }
     
      if (attrValue[0] == '!') {
         HASH_FIND_STR(attr->vals, attrValue+1, attrval);
         if (attrval == NULL) {
            printf("Error in description for object %s. Unknown attribute value %s for attribute %s.\n", bestName, attrValue+1, attrName);
            return 0;
         }
         if (foundNode->attrValues[attr->idx] == NULL) {
            foundNode->attrValues[attr->idx] = (int*)malloc(sizeof(int)*attr->nvals);
            for (i = 0; i < attr->nvals; i++)
               foundNode->attrValues[attr->idx][i] = 0;
         }
         foundNode->attrValues[attr->idx][attrval->idx] = -1;
         for (i = 0; i < attr->nvals; i++) {
            if (foundNode->attrValues[attr->idx][i] != -1) break;
         }
         if (i == attr->nvals) {
            printf("Error in attribute %s description for object %s. Attribute values are exhaustive. They cannot all be false.\n", attrName, bestName);
            return 0;
         }
         addAttributeToKB(kb, foundNode, attrName, attrValue+1, 0);
      } else {
         HASH_FIND_STR(attr->vals, attrValue, attrval);
         if (attrval == NULL) {
            printf("Error in description for object %s. Unknown attribute value %s for attribute %s.\n", bestName, attrValue, attrName);
            return 0;
         }
         if (foundNode->assignedAttr[attr->idx] != NULL) {
            printf("Error in attribute %s description for object %s. Attribute values are mutually exclusive.\n", attrName, bestName);
            return 0;
         }
         foundNode->assignedAttr[attr->idx] = attrval;
         addAttributeToKB(kb, foundNode, attrName, attrValue, 1);
      } 
      iter = strchr(iter, ',');
      if (iter != NULL) iter++;
   }
   return 1;
}

/**
 * Read in an attribute line of a class description
 * and add that information to the KB
 */
int readInAttribute(TMLKB* kb, TMLClass* cl, char* attrName, char* line) {
   char attr_fmt_str[50];
   char attr_fmt_str2[50];
   char attrValue[MAX_NAME_LENGTH+1];
   char comma[2];
   int n = 0;
   int i;
   char* iter = line;
   TMLClass* tmpcl;
   TMLPart* part;
   TMLRelation* rel;
   TMLAttribute* attr;
   TMLAttribute* foundattr;
   TMLAttrValue* attrval;
   TMLAttrValue* attrval2;
   TMLAttrValue* avtmp;
   float wt;
   int correctScan;
   int subclIdx;

   HASH_FIND_STR(kb->classNameToPtr, attrName, tmpcl);
   if (tmpcl != NULL) {
      printf("Error in attribute line for class %s. %s is the name of a class so it cannot also be the name of an attribute.\n", cl->name, attrName);
      return 0;
   }
   part = getPart(cl, attrName);
   if (part != NULL) {
      printf("Error in attribute line for class %s. %s is the name of a part for this class so it cannot also be the name of an attribute.\n", cl->name, attrName);
      return 0;
   }
   tmpcl = cl->par;
   while (tmpcl != NULL) {
      part = getPart(tmpcl, attrName);
      if (part != NULL) {
         printf("Error in attribute line for class %s. %s is the name of a part for an ancestor of this class so it cannot also be the name of an attribute.\n", cl->name, attrName);
         return 0;
      }
      rel = getRelation(tmpcl, attrName);
      if (rel != NULL) {
         printf("Error in attribute line for class %s. %s is the name of a relation for an ancestor of this class so it cannot also be the name of an attribute.\n", cl->name, attrName);
         return 0;
      }
      tmpcl = tmpcl->par;
   }
   attr = getAttribute(cl, attrName);
   if (attr != NULL) {
      printf("Error: Duplicate attribute lines for %s appear for class %s.\n", attrName, cl->name);
      return 0;
   }
   snprintf(attr_fmt_str, 50, " %%%d[a-zA-Z0-9:] %%f %%1s", MAX_NAME_LENGTH);
   snprintf(attr_fmt_str2, 50, " %%%d[!a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);
   while (TRUE) {
      iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   if (n == 0) {
      printf("Error on description for attribute %s. Attribute description should be AttributeName AttributeValue1 wt, AttributeValue2 wt, ...;\n", attrName);
      return 0;
   }

   attr = (TMLAttribute*)malloc(sizeof(TMLAttribute));
   attr->name = strdup(attrName);
   attr->vals = NULL;
   attr->nvals = 0;
   attr->idx = HASH_COUNT(cl->attr);
   attr->defaultAttr = 0;
   attr->defaultAttrForSubcl = NULL;
   HASH_ADD_KEYPTR(hh, cl->attr, attr->name, strlen(attr->name), attr);
   cl->nattr++;

   iter = line;
   for (i = 0; i < n; i++) {
      correctScan = sscanf(iter, attr_fmt_str, attrValue, &wt, comma);
      if ((i != n-1 && correctScan != 3) || (i == n-1 && correctScan != 2)
         || (i != n-1 && correctScan == 3 && comma[0] != ',')) {
         correctScan = sscanf(iter, attr_fmt_str2, attrValue, comma);
         if ((i != n-1 && correctScan != 2) || (i == n-1 && correctScan != 1)
            || (i != n-1 && correctScan == 2 && comma[0] != ',')) {
            printf("Error in description for attribute %s in class %s.\n", attrName, cl->name);
            return 0;
         }
         wt = 0.0;
      }
      HASH_FIND_STR(attr->vals, attrValue, attrval);
      if (attrval != NULL) {
         printf("Duplicate attribute value %s in description for attribute %s in class %s.\n", attrValue, attrName, cl->name);
         return 0;
      }
      attr->nvals++;
      attrval = (TMLAttrValue*)malloc(sizeof(TMLAttrValue));
      attrval->name = strdup(attrValue);
      attrval->wt = wt;
      attrval->idx = HASH_COUNT(attr->vals);
      HASH_ADD_KEYPTR(hh, attr->vals, attrval->name, strlen(attrval->name), attrval);
      iter = strchr(iter, ',');
      if (iter != NULL)
          iter++;
   }
   tmpcl = cl->par;
   subclIdx = cl->subclIdx;
   while (tmpcl != NULL) {
      foundattr = getAttribute(tmpcl, attrName);
      if (foundattr != NULL) {
         foundattr->defaultAttr = 1;
         if (foundattr->defaultAttrForSubcl == NULL) {
            foundattr->defaultAttrForSubcl = (int*)malloc(sizeof(int)*tmpcl->nsubcls);
            for (i = 0; i < tmpcl->nsubcls; i++)
               foundattr->defaultAttrForSubcl[i] = 0;
         }
         foundattr->defaultAttrForSubcl[subclIdx] = 1;
         HASH_ITER(hh, foundattr->vals, attrval, avtmp) {
            HASH_FIND_STR(attr->vals, attrval->name, attrval2);
            if (attrval2 != NULL) {
               attrval2->wt += attrval->wt;
            } else {
               attrval2 = (TMLAttrValue*)malloc(sizeof(TMLAttrValue));
               attrval2->name = strdup(attrValue);
               attrval2->wt = attrval->wt;
               attrval2->idx = HASH_COUNT(attr->vals);
               HASH_ADD_KEYPTR(hh, attr->vals, attrval2->name, strlen(attrval2->name), attrval2);
               attr->nvals++;
            }
         }
         break;
      }
      subclIdx = tmpcl->subclIdx;
      tmpcl = tmpcl->par;
   }
   return 1;
}

/**
 * Read in relations line of a class description
 * and add that information to the KB
 */
int readInRelations(TMLKB* kb, TMLClass* cl, char* line) {
   char rel_fmt_str[60];
   char rel_noargs_fmt_str[50];
   char rel_noargs2_fmt_str[50];
   char rel_hard_fmt_str[60];
   char rel_noargs_hard_fmt_str[50];
   char rel_noargs2_hard_fmt_str[50];
   char relStr[MAX_NAME_LENGTH+1];
   char comma[2];
   char* relName;
   int pol;
   char argsStr[MAX_LINE_LENGTH+1];
   char arg_fmt_str[50];
   char argStr[MAX_NAME_LENGTH+1];
   float wt;
   int hard;
   int correctScan;
   char* iter = line;
   char* aiter;
   int a;
   TMLPart* part;
   TMLClass* tempcl;
   TMLClass* tempcl2;
   int n = 0;
   TMLRelation* rel;
   TMLRelation* newRel;
   int nargs;
   int* pc;
   char** pn;
   int* pp;
   int added;
   QNode* maxArgsQueue = NULL;
   QNode* curr;
   IntAccessor* ia;
   int rec;
   int numCombs;
   int validArgGrounding;
   int* args;
   int high;
   int low;
   char rparen[2];

   int r, i, c;
   int j, k;
   char* iter2;

   while (TRUE) {
      iter2 = strchr(iter, ')');
      if (iter2 != NULL)
         iter = strchr(iter2, ',');
      else
         iter = strchr(iter, ',');
      if (iter != NULL) {iter++; n++;}
      else { n++; break; }
   }
   if (n == 0) {
      printf("Malformed relation descriptions for class %s. Expected entries of the form R(Part1,...,Partn) wt, HardRule(Part1), ...;\n", cl->name);
      return 0;
   }
   snprintf(rel_fmt_str, 60, " %%%d[!a-zA-Z0-9:] (%%%d[] a-zA-Z0-9:[,]) %%f %%1s", MAX_NAME_LENGTH, MAX_LINE_LENGTH);
   snprintf(rel_noargs_fmt_str, 50, " %%%d[!a-zA-Z0-9:] ( ) %%f %%1s", MAX_NAME_LENGTH);
   snprintf(rel_noargs2_fmt_str, 50, " %%%d[!a-zA-Z0-9:] %%f %%1s", MAX_NAME_LENGTH);
   snprintf(rel_hard_fmt_str, 60, " %%%d[!a-zA-Z0-9:] (%%%d[] a-zA-Z0-9:[,] %%1[)] %%1s", MAX_NAME_LENGTH, MAX_LINE_LENGTH);
   snprintf(rel_noargs_hard_fmt_str, 50, " %%%d[!a-zA-Z0-9:] ( %%1[)] %%1s", MAX_NAME_LENGTH);
   snprintf(rel_noargs2_hard_fmt_str, 50, " %%%d[!a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);
   snprintf(arg_fmt_str, 50, " %%%d[a-zA-Z0-9:] %%1s", MAX_NAME_LENGTH);

   iter = line;
   for (r = 0; r < n; r++) {
      hard = 0;
      correctScan = sscanf(iter, rel_fmt_str, relStr, argsStr, &wt, comma);
      if ((r != n-1 && correctScan != 4) || (r != n-1 && correctScan == 4 && comma[0] != ',') ||
         (r == n-1 && correctScan != 3)) {
         correctScan = sscanf(iter, rel_noargs_fmt_str, relStr, &wt, comma);
         if ((r != n-1 && correctScan != 3) || (r != n-1 && correctScan == 3 && comma[0] != ',')
               || (r == n-1 && correctScan != 2)) {
            correctScan = sscanf(iter, rel_noargs2_fmt_str, relStr, &wt, comma);
            if ((r != n-1 && correctScan != 3) || (r != n-1 && correctScan == 3 && comma[0] != ',')
                  || (r == n-1 && correctScan != 2)) {
               correctScan = sscanf(iter, rel_hard_fmt_str, relStr, argsStr, rparen, comma);
               if ((r != n-1 && correctScan != 4) || (r != n-1 && correctScan == 4 && comma[0] != ',')
                     || (r == n-1 && correctScan != 3)) {
                  correctScan = sscanf(iter, rel_noargs_hard_fmt_str, relStr, rparen, comma);
                  if ((r != n-1 && correctScan != 3) || (r != n-1 && correctScan == 3 && comma[0] != ',')
                        || (r == n-1 && correctScan != 2)) {
                     correctScan = sscanf(iter, rel_noargs2_hard_fmt_str, relStr, comma);
                     if ((r != n-1 && correctScan != 2) || (r != n-1 && correctScan == 2 && comma[0] != ',')
                           || (r == n-1 && correctScan != 1)) {
                        printf("Malformed relation descriptions for class %s. Expected entries of the form R(Part1,...,Partn) wt, HardRule(Part1), ...;\n", cl->name);
                        return 0;
                     } else { hard = 1; argsStr[0] = '\0'; }
                  } else { hard = 1; argsStr[0] = '\0'; }
               } else hard = 1;
            } else argsStr[0] = '\0';
         } else {
            argsStr[0] = '\0';
         }
      }
      if (strpbrk(relStr, "0123456789") == relStr) {
         printf("Error in relation description of %s. Relation names must begin with a letter. %s does not.\n", cl->name, relStr);
         return 0;
      }
      aiter = strchr(relStr, '!');
      if (aiter != NULL) {
         if (aiter != relStr) {
            printf("Error in relation description of %s. Exclamation points should be used to denote negated relations. Relation names cannot contain '!'.\n", cl->name);
            return 0;
         }
         pol = 0;
         relName = relStr+1;
      } else {
         pol = 1;
         relName = relStr;
      }
      rel = getRelation(cl, relName);
      if (rel == NULL) {
         if (argsStr[0] == '\0') {
            rel = TMLRelationNew(relName, 0, cl->nsubcls);
            HASH_ADD_KEYPTR(hh, cl->rel, rel->name, strlen(rel->name), rel);
         } else {
            nargs = 0;
            aiter = argsStr;
            while (TRUE) {
               aiter = strchr(aiter, ',');
               if (aiter != NULL) {aiter++; nargs++;}
               else { nargs++; break; }
            }
            aiter = argsStr;
            rel = TMLRelationNew(relName, nargs, cl->nsubcls);
            for (a = 0; a < nargs; a++) {
               correctScan = sscanf(aiter, arg_fmt_str, argStr, comma);
               if ((a != nargs-1 && correctScan != 2) || (a == nargs-1 && correctScan != 1)) {
                  printf("Malformed relation descriptions for class %s. Expected entries of the form R(Part1,...,Partn) wt, HardRule(Part1), ...;\n", cl->name);
                  return 0;
               }
               rel->argClass[a] = -1;
               rel->argPartName[a] = NULL;
               part = getPart(cl, argStr);
               if (part != NULL) {
                  if (part->defaultPart == 0) {
                     rel->argClass[a] = cl->id;
                     rel->argPartName[a] = part->name;
                     if (rel->numposs != -1) {
                        rel->numposs *= part->n;
                     }
                  } else {
                     rel->argClass[a] = -1;
                     rel->argPartName[a] = part->name;
                     rel->defaultRel = 1;
                     if (rel->defaultRelForSubcl == NULL) {
                        rel->defaultRelForSubcl = (int*)malloc(sizeof(int)*cl->nsubcls);
                        for (c = 0; c < cl->nsubcls; c++) rel->defaultRelForSubcl[c] = 0;
                        if (part->defaultPartForSubcl != NULL) {
                           for (c = 0; c < cl->nsubcls; c++) {
                              if (part->defaultPartForSubcl[c] == 1)
                                 rel->defaultRelForSubcl[c] = 1;
                              else
                                 rel->defaultRelForSubcl[c] = 0;
                           }
                        }
                     } else {
                        for (c = 0; c < cl->nsubcls; c++) {
                           if (part->defaultPartForSubcl[c] == 1) {
                              rel->defaultRelForSubcl[c] = 1;
                           }
                        }
                     }
                  }
               } else if (part == NULL && cl->par != NULL) {
                  tempcl = cl->par;
                  tempcl2 = cl;
                  while (tempcl != NULL) {
                     part = getPart(tempcl, argStr);
                     if (part != NULL) {
                        if (part->defaultPart == 0 || (part->defaultPartForSubcl != NULL && part->defaultPartForSubcl[tempcl2->subclIdx] == 0)) {
                           rel->argClass[a] = tempcl->id;
                           rel->argPartName[a] = part->name;
                           if (rel->numposs != -1) {
                              rel->numposs *= part->n;
                           }
                        } else {
                           rel->argClass[a] = -1;
                           rel->argPartName[a] = part->name;
                           rel->defaultRel = 1;
                           if (rel->defaultRelForSubcl == NULL) {
                              rel->defaultRelForSubcl = (int*)malloc(sizeof(int)*cl->nsubcls);
                              for (i = 0; i < cl->nsubcls; i++) rel->defaultRelForSubcl[i] = 0;
                           }
                           for (c = 0; c < cl->nsubcls; c++) {
                              if (part->defaultPartForSubcl[c] == 1)
                                 rel->defaultRelForSubcl[c] = 1;
                           }
                        }
                        break;
                     } else {
                        tempcl2 = tempcl;
                        tempcl = tempcl->par;
                     }
                  }
               }

               if (rel->argPartName[a] == NULL) {
                  printf("Error in relation description for class %s: There is no part named %s in class %s or one of its ancestor classes.\n", cl->name, argStr, cl->name);
                  return 0;
               }
               aiter = strchr(aiter, ',');
               if (aiter == NULL) break;
               aiter++;
            }
            if (rel->defaultRel == 1) {
               added = 0;
               for (c = 0; c < cl->nsubcls; c++) {
                  newRel = copyTMLRelation(rel);
                  rec = pushDefaultRelationDown(kb, cl->subcl[c], newRel);
                  if (rec == 0) { // TODO: why was this FALSE &&...
                     rel->defaultRelForSubcl[c] = 0;
                  } else {
                     rel->defaultRelForSubcl[c] = 1;
                  }
               }
               pc = rel->argClass;
               pn = rel->argPartName;
               for (a = 0; a < rel->nargs; a++) {
                  if (*pc == -1) {
                     tempcl = cl;
                     while (TRUE) {
                        part = getPart(tempcl, *pn);
                        if (part != NULL) {
                           *pc = tempcl->id;
                           if (rel->numposs != -1) {
                              rel->numposs *= part->n;
                           }
                           break;
                        }
                        tempcl = tempcl->par;
                     }
                  }
                  pc++;
                  pn++;
               }
            }
            HASH_ADD_KEYPTR(hh, cl->rel, rel->name, strlen(rel->name), rel);
         }
         cl->nrels++;
      } else {
         // rel seen already. make sure it looks the same
         if (rel->nargs != 0 && argsStr[0] == '\0') {
            printf("Error in relations for class %s: Contradictory definitions for relation %s.\n", cl->name, relName);
            return 0;
         }
         if (rel->nargs == 0 && argsStr[0] != '\0') {
            printf("Error in relations for class %s: Contradictory definitions for relation %s.\n", cl->name, relName);
            return 0;
         }
         aiter = argsStr;
         for (a = 0; a < rel->nargs; a++) {
            correctScan = sscanf(aiter, arg_fmt_str, argStr, comma);
            if ((a != rel->nargs-1 && correctScan != 2) || (a == rel->nargs-1 && correctScan != 1)) {
               printf("Malformed relation description for %s in class %s. Expected entries of the form R(Part1,...,Partn) wt, HardRule(Part1), ...;\n", relName, cl->name);
               return 0;
            }
            part = getPart(cl, argStr);
            if (strcmp(argStr, rel->argPartName[a]) != 0) {
               printf("Error in relations for class %s: Contradictory definitions for relation %s.\n", cl->name, relName);
               return 0;
            }
            aiter = strchr(aiter, ',');
            if (aiter == NULL) break;
            aiter++;
         }
      }
      if (pol == 0) {
         if (hard == 1) {
           rel->nwt = log(10000);
           rel->pwt = log(0.00001);
           if (rel->hard == 1) {
               printf("Error in relations for class %s: Rule %s is already defined as being always negative.", cl->name, relName);
               return 0;
            }
            rel->hard = -1;
         } else
            rel->nwt += wt;
      } else {
         if (hard == 1) {
           rel->pwt = log(10000);
           rel->nwt = log(0.00001);
           if (rel->hard == -1) {
               printf("Error in relations for class %s: Rule %s is already defined as being always negative.", cl->name, relName);
               return 0;
            }
            rel->hard = 1;
         } else
            rel->pwt += wt;
      }
      tempcl = cl->par;
      k = cl->subclIdx;
      while (tempcl != NULL) {
         newRel = getRelation(tempcl, rel->name);
         if (newRel != NULL) {
            if (newRel->defaultRel == 0) {
               if (newRel->defaultRelForSubcl == NULL) {
                  newRel->defaultRelForSubcl = (int*)malloc(sizeof(int)*tempcl->nsubcls);
                  for (j = 0; j < tempcl->nsubcls; j++)
                     newRel->defaultRelForSubcl[j] = 0;
               }
               newRel->defaultRel = 1;
               newRel->defaultRelForSubcl[k] = 1;

               rel->pwt += newRel->pwt;
               rel->nwt += newRel->nwt;
            } else {
               newRel->defaultRelForSubcl[k] = 1;
               rel->pwt += newRel->pwt;
               rel->nwt += newRel->nwt;
            }
            rel->overrideRel = 1;
            break;
         }
         k = tempcl->subclIdx;
         tempcl = tempcl->par;
      }
      iter2 = strchr(iter, ')');
      if (iter2 == NULL)
         iter = strchr(iter, ',');
      else
         iter = strchr(iter2, ',');
      if (iter != NULL) iter++;
   }
   return 1;
}

/**
 * Read in information for one object description
 */
int readInOneObject(TMLKB* kb, char* objectName, Node* node, TMLClass* cl, FILE* tmlFactFile, int linenum) {
   char keyword_fmt_str[15];
   char* line;
   char* restOfLine = NULL;
   int correctScan;
   char endline[2];
   int lineType;
   fpos_t pos;
   int sawSubcl = 0;
   char* iter;
   char attrName[MAX_NAME_LENGTH+1];
   TMLClass* finecl = cl;
   Node* tmpnode = node;

   fgetpos(tmlFactFile, &pos);
   while (TRUE) {
      line = getLineToSemicolon(tmlFactFile, &restOfLine, &linenum);
      if (line == NULL) {
         printf("Error ending object %s description. Missing '}'.\n", objectName);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      if (strchr(line, '{') != NULL) {
         printf("Error in object %s description. Unexpected '{'.\n", objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      if (strchr(line,'}') != NULL) {
         correctScan = sscanf(line, "%1[}]", endline);
         if (correctScan != 1) {
            printf("Error ending object %s description: Expecting '}'\n", objectName);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
         break;
      }
      lineType = determineObjectLineType(kb, finecl, line);
      if (lineType == EMPTY_LINE) continue;
      if (lineType == SUBCLASS) {
         correctScan = readInObjectClasses(kb, node, tmlFactFile, linenum, line);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
         fillOutSubclasses(node);
         sawSubcl = 1;
      } else if (lineType == SUBPART) {
         continue;
      } else if (lineType == RELATION) {
         continue;
      } else if (lineType == ATTRIBUTE) {
         continue;
      } else {
         printf("Error on line \"%s\" in description of object %s.\n", line, objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      free(line);
   }
   if (!sawSubcl) fillOutSubclasses(node);
   while (finecl->nsubcls != 0) {
      if (tmpnode->assignedSubcl != -1 && tmpnode->subclMask == NULL) {
         finecl = finecl->subcl[tmpnode->assignedSubcl];
         tmpnode = tmpnode->subcl;
      } else if (tmpnode->assignedSubcl != -1) {
         finecl = finecl->subcl[tmpnode->assignedSubcl];
         tmpnode = &(tmpnode->subcl[tmpnode->assignedSubcl]);
      } else
         break;
   }
   fsetpos(tmlFactFile, &pos);
   while (TRUE) {
      line = getLineToSemicolon(tmlFactFile, &restOfLine, &linenum);
      if (line == NULL) {
         printf("Error ending object %s description. Missing '}'.\n", objectName);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      if (strchr(line, '{') != NULL) {
         printf("Error in object %s description. Unexpected '{'.\n", objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      if (strchr(line,'}') != NULL) {
         correctScan = sscanf(line, "%1[}]", endline);
         if (correctScan != 1) {
            printf("Error ending object %s description: Expecting '}'\n", objectName);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
         break;
      }
      lineType = determineObjectLineType(kb, finecl, line);
      if (lineType == EMPTY_LINE) continue;
      if (lineType == SUBCLASS) {
         continue;
      } else if (lineType == SUBPART) {
         correctScan = readInObjectSubparts(kb, node, objectName, tmlFactFile, linenum, line);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
      } else if (lineType == RELATION) {
         continue;
      } else if (lineType == ATTRIBUTE) {
         continue;
      } else {
         printf("Error on line \"%s\" in description of object %s. Cannot determine line type.\n", line, objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      free(line);
   }
   fsetpos(tmlFactFile, &pos);
   while (TRUE) {
      line = getLineToSemicolon(tmlFactFile, &restOfLine, &linenum);
      if (line == NULL) {
         printf("Error ending object %s description. Missing '}'.\n", objectName);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      if (strchr(line, '{') != NULL) {
         printf("Error in object %s description. Unexpected '{'.\n", objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      if (strchr(line,'}') != NULL) {
         correctScan = sscanf(line, "%1[}]", endline);
         if (correctScan != 1) {
            printf("Error ending object %s description: Expecting '}'\n", objectName);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
         break;
      }
      lineType = determineObjectLineType(kb, finecl, line);
      if (lineType == EMPTY_LINE) continue;
      if (lineType == SUBCLASS) {
         continue;
      } else if (lineType == SUBPART) {
         continue;
      } else if (lineType == RELATION) {
         correctScan = readInObjectRelations(kb, node, objectName, tmlFactFile, linenum, line);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
      } else if (lineType == ATTRIBUTE) {
         snprintf(keyword_fmt_str, 15, " %%%ds", MAX_NAME_LENGTH);
         correctScan = sscanf(line, keyword_fmt_str, attrName);
         if (correctScan != 1) {
            printf("Error in description for object %s. Malformed attribute line \"%s\".\n", objectName, line);
            return 0;
         }
         iter = strstr(line, attrName)+strlen(attrName);
         correctScan = readInObjectAttribute(NULL, node, objectName, attrName, iter);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
      } else {
         printf("Error on line %d in description of object %s. Cannot determine line type.\n", linenum, objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      free(line);
   }
}

/**
 * Reads in the subclasses and subparts of one class from the TML rule file.
 * (Reading in the relations occurs in the second pass through the .tml file.)
 *
 * @param kb            TML KB
 * @param className     name of the class being read in
 * @param rootCl        If this class is a root class, return the class in this arg
 * @param tmlRuleFile   .tml file
 * @param linenum       current line number when beginning to read tmlRuleFile for this class
 * @param id            id of class
 * @param counts        FOR CHLOE: specifies if numbers in .tml file are raw counts or weights
 * @return the new current line number
 */
int readInOneTMLClass(TMLKB* kb, const char* className, TMLClass** rootCl, FILE* tmlRuleFile, int linenum, int id, int counts, int first) {
   char* line;
   char subclStr[MAX_NAME_LENGTH];
   char partStr[MAX_NAME_LENGTH];
   char part[MAX_NAME_LENGTH];
   char keyword[MAX_NAME_LENGTH];
   char numStr[MAX_NAME_LENGTH];
   char relStr[MAX_NAME_LENGTH];
   char endline[2];
   char keyword_fmt_str[15];
   char** relStrs;
   float wt;
   int cnt;
   int totalcnt;
   int n;
   int i, j, k, p;
   int r;
   int numParts;
   int correctScan;
   TMLClass* cl;
   TMLClass* subcl;
   TMLClass* parcl;
   TMLClass* tempcl;
   TMLClass* tempcl2;
   char* iter;
   char* piter;
   char* comma;
   char* partName;
   TMLPart* tmlpart;
   TMLPart* foundpart;
   int pol;
   int hard;
   int partType;
   int hasParts;
   char* relationStr;
   TMLRelation* rel;
   Name_and_Ptr* name_and_ptr;
   char* tempStr;
   float norm;
   int firstFind;
   char* restOfLine = NULL;
   int subclLine = 0;
   int subpartLine = 0;
   int relLine = 0;

   snprintf(keyword_fmt_str, 15, " %%%ds", MAX_NAME_LENGTH);
   // If no superclass has been specified (and this isn't the Top Class,
   // send back a pointer to be checked later to see if it is a lost root.
   HASH_FIND_STR(kb->classNameToPtr, className, cl);
   if (getTopClass(kb) != NULL) {
      if (cl->subclIdx == -1) *rootCl = cl;
   } else {
      kb->topcl = cl;
   }

   while (TRUE) {
      line = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
      if (line == NULL) {
         printf("Error ending class %s description. Missing '}'.\n", className);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlRuleFile);
         exit(1);
      }
      if (strchr(line, '{') != NULL) {
         printf("Error in class %s description. Unexpected '{'.\n", className);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlRuleFile);
         exit(1);
      }
      if (strchr(line,'}') != NULL) {
         correctScan = sscanf(line, "%1[}]", endline);
         if (correctScan != 1) {
            printf("Error ending class %s description: Expecting '}'\n", className);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
         break;
      }
      correctScan = sscanf(line, keyword_fmt_str, keyword);
      if (correctScan != 1) {
         // empty ; line
         free(line);
         continue;
      }
      if (strcmp(keyword,"subclasses") == 0) {
         if (!first) continue;
         if (subclLine == 1) {
            printf("Error: There are two lines specifying subclasses for class %s.\n", cl->name);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         } else subclLine = 1;
         iter = strstr(line, "subclasses")+10;
         correctScan = sscanf(iter, keyword_fmt_str, keyword);
         if (correctScan != 1) {free(line); continue;}
         correctScan = readInSubclasses(kb, cl, iter);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
      } else if (strcmp(keyword, "subparts") == 0) {
         if (!first) continue;
         if (subpartLine == 1) {
            printf("Error: There are two lines specifying subparts for class %s.\n", cl->name);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         } else subpartLine = 1;
         iter = strstr(line, "subparts")+8;
         correctScan = sscanf(iter, keyword_fmt_str, keyword);
         if (correctScan != 1) {free(line); continue;}
         correctScan = readInSubparts(kb, cl, iter);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
      } else if (strcmp(keyword, "relations") == 0) {
         if (first) continue;
         if (relLine == 1) {
            printf("Error: There are two lines specifying subclasses for class %s.\n", cl->name);
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         } else relLine = 1;
         iter = strstr(line, "relations")+9;
         correctScan = sscanf(iter, keyword_fmt_str, keyword);
         if (correctScan != 1) {free(line); continue;}
         correctScan = readInRelations(kb, cl, iter);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
      } else {
         if (first) continue;
         // attribute
         iter = strstr(line, keyword)+strlen(keyword);
         correctScan = readInAttribute(kb, cl, keyword, iter);
         if (correctScan == 0) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
      }
      free(line);
   }
}

/**
 * If a class has a relation that uses subparts that are overridden in subclasses of
 * that class, this function is called recursively to create relation objects for descendants.
 * This is to ensure the proper number of ground relation literals are counted for each
 * class.
 *
 * @param kb            TML KB
 * @param cl            current class
 * @param rel           relation being pushed
 */
int pushDefaultRelationDown(TMLKB* kb, TMLClass* cl, TMLRelation* rel) {
   int found = 0;
   int p;
   int c;
   TMLPart* part;
   int* pc = rel->argClass;
   char** pn = rel->argPartName;   
   int notDefault = 1;
   TMLRelation* newRel;
   int rec = 0;
   int lowerchange = 0;
   int added = 0;
   TMLClass* tempcl;

   for (p = 0; p < rel->nargs; p++) {
      if (*pc == -1) {
         part = getPart(cl, *pn);
         if (part != NULL) {
            if (part->defaultPart == 0) {
               *pc = cl->id;
               rel->numposs *= part->n;
               found = 1;
            } else
               notDefault = 0;
         } else {
            notDefault = 0;
         }
      }
      pn++;
      pc++;
   }
   if (notDefault == 1) {
      rel->defaultRel = 0;
      HASH_ADD_KEYPTR(hh, cl->rel, rel->name, strlen(rel->name), rel);
      cl->nrels++;
      return found;
   } else {
      if (cl->nsubcls != 0) {
         if (found == 1)
            rel->defaultRelForSubcl = (int*)malloc(sizeof(int)*cl->nsubcls); 
        //if found == 1 (so there was a change, but no further changes, add rel
         for (c = 0; c < cl->nsubcls; c++) {
            newRel = copyTMLRelation(rel);
            lowerchange = pushDefaultRelationDown(kb, cl->subcl[c], newRel);
            if (found == 1) {
               if (lowerchange == 0 && added == 0) {
                  pc = rel->argClass;
                  pn = rel->argPartName;
                  for (p = 0; p < rel->nargs; p++) {
                     if (*pc == -1) {
                        tempcl = cl;
                        while (TRUE) {
                           part = getPart(tempcl, *pn);
                           if (part != NULL) {
                              *pc = tempcl->id;
                              rel->numposs *= part->n;
                              break;
                           }
                           tempcl = tempcl->par;
                        }
                     }
                     pc++;
                     pn++;
                  }
                  HASH_ADD_KEYPTR(hh, cl->rel, rel->name, strlen(rel->name), rel);
                  cl->nrels++;
                  added = 1;
                  rel->defaultRelForSubcl[c] = 0;
               } else if (lowerchange == 0) {
                  rel->defaultRelForSubcl[c] = 0;
               } else if (lowerchange == 1)  {
                  rec = 1;
                  rel->defaultRelForSubcl[c] = 1;
               }
            }
         }
      } else {
         rel->defaultRel = 0;
         pc = rel->argClass;
         pn = rel->argPartName;
         for (p = 0; p < rel->nargs; p++) {
            if (*pc == -1) {
               tempcl = cl;
               while (TRUE) {
                  part = getPart(tempcl, *pn);
                  if (part != NULL) {
                     *pc = tempcl->id;
                     rel->numposs *= part->n;
                     break;
                  }
                  tempcl = tempcl->par;
               }
            }
            pc++;
            pn++;
         }
         HASH_ADD_KEYPTR(hh, cl->rel, rel->name, strlen(rel->name), rel);
         cl->nrels++;
         added = 1;
      }
   }
   if (added == 0) {
      freeTMLRelation(rel);
   }
   if (added == 1 || rec == 1) return 1;
   return 0;
}

/**
 * Recursive function to compute the weight of the current state of the world
 *
 * @param node       current node in the SPN
 * @param assignedClassBySuperPart class of node defined by its subpart relation to
 *                   its superpart. This is important when subclasses redefine a part
 *                   to be of a subclass of the class it was first defined.
 * @param spn_func   a function that either computes a sum or a max of an array
 *                   of floats
 * @param recompute  if recompute == 1, recompute the logZ for the node regardless
 *                   of if the node has been changed since the last computation
 *                   (Needed if switching between MAP and query inference)
 * @return     the partition function at node
 */
float computeLogZ(Node* node, TMLClass* assignedClassBySuperpart, float(*spn_func)(float* arr, int num, int* idx), int recompute) {
   int i, j, k;
   int c;
   float logZ = 0.0;
   float* subclZ;
   int n;
   TMLClass* cl = node->cl;
   Node** partNodes;
   Node* partNode;
   Node* subclNode;
   TMLRelation* rel;
   TMLRelation* tmp;
   TMLAttribute* attr;
   TMLAttribute* tmpa;
   TMLClass* tmpcl;
   TMLPart* part;
   TMLPart* tmppart;
   int** relValues = node->relValues;
   Node*** parts = node->part;
   int maxIdx = -1;
   int assignedSubcl;
   int descendantIdx = isDescendant(assignedClassBySuperpart, node->cl);

   if (node->changed == 0 && recompute == 0 && descendantIdx == -1) return node->logZ;
   if (descendantIdx != -1 && node->assignedSubcl != -1 && node->assignedSubcl != descendantIdx) {
      node->logZ = log(0.0);
      node->changed = 0;
      node->maxSubcl = maxIdx;
      return node->logZ;
   }

   if (node->assignedSubcl != -1 && cl->nsubcls != 0) {
         assignedSubcl = node->assignedSubcl;
      if (node->subclMask == NULL)
         logZ += cl->wt[assignedSubcl]+computeLogZ(node->subcl, assignedClassBySuperpart, spn_func, recompute);
      else
         logZ += cl->wt[assignedSubcl]+computeLogZ(&(node->subcl[assignedSubcl]), assignedClassBySuperpart, spn_func, recompute);
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (rel->defaultRel == 0) {
            logZ += relWeight((*relValues), rel);
         } else {
            if (rel->defaultRelForSubcl[assignedSubcl] == 0) {
               logZ += relWeight((*relValues), rel);
            }
         }
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         if (attr->defaultAttr == 0) {
            logZ += attrWeight(node, attr);
         } else {
            if (attr->defaultAttrForSubcl[assignedSubcl] == 0) {
               logZ += attrWeight(node, attr);
            }
         }
      }
      HASH_ITER(hh, cl->part, part, tmppart) {
         partNodes = *parts;
         if (part->defaultPart == 0 || part->defaultPartForSubcl[assignedSubcl] == 0) {
            for (j = 0; j < part->n; j++) {
               partNode = *partNodes;
               logZ += computeLogZ(partNode, part->cl, spn_func, recompute);
               partNodes++;
            }
         }
         parts++;
      }
   } else if (node->assignedSubcl == -1 && cl->nsubcls != 0 && node->subclMask == NULL) {
      subclZ = (float*)malloc(sizeof(float)*cl->nsubcls);
      for (i = 0; i < cl->nsubcls; i++) {
         subclNode = &(node->subcl[i]);
         if (descendantIdx != -1 && descendantIdx != i)
            subclZ[i] = log(0.0);
         else
            subclZ[i] = cl->wt[i]+computeLogZ(subclNode, assignedClassBySuperpart, spn_func, recompute);
      }
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (rel->defaultRel == 0) {
            logZ += relWeight((*relValues), rel);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (rel->defaultRelForSubcl[j] == 0) {
                  subclZ[j] += relWeight((*relValues), rel);
               }
            }
         }
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         if (attr->defaultAttr == 0) {
            logZ += attrWeight(node, attr);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (attr->defaultAttrForSubcl[j] == 0) {
                  subclZ[j] += attrWeight(node, attr);
               }
            }
         }
      }
      HASH_ITER(hh, cl->part, part, tmppart) {
         if (part->defaultPart == 0) {
            partNodes = *parts;
            for (j = 0; j < part->n; j++) {
               partNode = *partNodes;
               logZ += computeLogZ(partNode, part->cl, spn_func, recompute);
               partNodes++;
            }
         } else {
            for (c = 0; c < cl->nsubcls; c++) {
               partNodes = *parts;
               if (part->defaultPartForSubcl[c] == 0) {
                  for (j = 0; j < part->n; j++) {
                     partNode = *partNodes;
                     subclZ[c] += computeLogZ(partNode, part->cl, spn_func, recompute);
                     partNodes++;
                  }
               }
            }
         }
         parts++;
      }
      logZ += spn_func(subclZ, cl->nsubcls, &maxIdx);
      free(subclZ);
   } else if (node->assignedSubcl == -1 && node->subclMask != NULL) {
      subclZ = (float*)malloc(sizeof(float)*cl->nsubcls);
      for (i = 0; i < cl->nsubcls; i++) {
         if (node->subclMask[i] != 1 || (descendantIdx != -1 && descendantIdx != i)) {
            subclZ[i] = log(0.0);
         } else {
            subclNode = &(node->subcl[i]);
            subclZ[i] = cl->wt[i]+computeLogZ(subclNode, assignedClassBySuperpart, spn_func, recompute);
         }
      }
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (rel->defaultRel == 0) {
            logZ += relWeight((*relValues), rel);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (node->subclMask[j] == 1 && rel->defaultRelForSubcl[j] == 0) {
                  subclZ[j] += relWeight((*relValues), rel);
               }
            }
         }
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         if (attr->defaultAttr == 0) {
            logZ += attrWeight(node, attr);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (node->subclMask[j] == 1 && attr->defaultAttrForSubcl[j] == 0) {
                  subclZ[j] += attrWeight(node, attr);
               }
            }
         }
      }
      HASH_ITER(hh, cl->part, part, tmppart) {
         if (part->defaultPart == 0) {
            partNodes = *parts;
            for (j = 0; j < part->n; j++) {
               partNode = *partNodes;
               logZ += computeLogZ(partNode, part->cl, spn_func, recompute);
               partNodes++;
            }
         } else {
            for (c = 0; c < cl->nsubcls; c++) {
               partNodes = *parts;
               if (node->subclMask[c] != 1) continue;
               if (part->defaultPartForSubcl[c] == 0) {
                  for (j = 0; j < part->n; j++) {
                     partNode = *partNodes;
                     subclZ[c] += computeLogZ(partNode, part->cl, spn_func, recompute);
                     partNodes++;
                  }
               }
            }
         }
         parts++;
      }
      logZ += spn_func(subclZ, cl->nsubcls, &maxIdx);
      free(subclZ);
   } else {
      HASH_ITER(hh, cl->rel, rel, tmp) {
         logZ += relWeight((*relValues), rel);
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
            logZ += attrWeight(node, attr);
      }
      HASH_ITER(hh, cl->part, part, tmppart) {
         partNodes = *parts;
         for (j = 0; j < part->n; j++) {
            partNode = *partNodes;
            logZ += computeLogZ(partNode, part->cl, spn_func, recompute);
            partNodes++;
         }
         parts++;
      }   
   }

   node->logZ = logZ;
   node->changed = 0;
   node->maxSubcl = maxIdx;
   return logZ;
}

/**
 * If node has a part named name with at least n parts, set the part_n
 * subpart of node to be partNode.
 * Recurses up the tree
 *
 * @param node       current node
 * @param partNode   subpart node name_n
 * @param name       name of subpart relation
 * @param n          number of the subpart relation
 */
void propagatePartUp(Node* node, Node* partNode, const char* name, int n) {
   TMLPart* part = getPart(node->cl, name);
   TMLPart* tmp;
   int i;

   if (part == NULL) {
      if (node->cl->par != NULL) {
         propagatePartUp(*(node->par), partNode, name, n);
      }
   } else {
      if (part->n >= (n+1)) {
         i = 0;
         HASH_ITER(hh, node->cl->part, part, tmp) {
            if (strcmp(part->name, name) == 0) break;
            i++;
         }
         node->part[i][n] = partNode;
      }
      if (node->cl->par != NULL) {
         propagatePartUp(*(node->par), partNode, name, n);
      }
   }
}

/**
 * Searches up a tree of classes (and through other branches)
 * to find a node with a subpart name_n.
 * If the part is found, maxParts is set to the maximum number of parts
 * in that subtree.
 *
 * @param node       current node
 * @param name       name of the subpart relation
 * @param n          index number of the subpart in question
 * @param maxParts   returns the maximum number of this subpart relation
 *                   if name_n is found
 * @return  the name_n subpart, NULL if not found
 */
Node* findPartUp(Node* node, const char* name, int n, int* maxParts) {
   TMLPart* part;
   TMLPart* tmp;
   int i;
   Node* par;
   Node* found;

   if (node->cl->par != NULL)
      par = *(node->par);
   else
      par = NULL;
   if (par == NULL) return NULL;

   part = getPart(par->cl, name);
   if (part == NULL) {
      found = findPartUp(par, name, n, maxParts);
      if (found != NULL) {
         return found;
      }
   } else {
      if (part->n >= (n+1)) {
         i = 0;
         HASH_ITER(hh, par->cl->part, part, tmp) {
            if (strcmp(part->name, name) == 0) break;
            i++;
         }
         found = par->part[i][n];
         *maxParts = part->maxNumParts;
         if (found != NULL) return found;
      } else if (part->overridePart == 1) {
         found = findPartUp(par, name, n, maxParts);
         if (found != NULL) return found;
      }
   }

   if (par->assignedSubcl != -1 && par->subclMask == NULL) return NULL;

   for (i = 0; i < node->cl->subclIdx; i++) {
      found = findPartDown(&(par->subcl[i]),name,n+1,0, maxParts);
      if (found != NULL) {
         return found;
      }
   }
   return NULL;
}

/**
 * Renames a node (recursive)
 *
 * @param kb         TML KB
 * @param node       current node
 * @param newName    new name
 */
void renameNode(TMLKB* kb, Node* node, char* newName) {
   int c;
   TMLClass* cl = node->cl;

   node->name = newName;
   if (cl->nsubcls == 0) return;
   if (node->assignedSubcl != -1 && node->subclMask == NULL) renameNode(kb, node->subcl, newName);
   else {
      for (c = 0; c < cl->nsubcls; c++)
         renameNode(kb, &(node->subcl[c]), newName);
   }   
}

/**
 * Looks down subclasses of a node until a subpart name_(n-1) is found
 * 
 * @param node       current node
 * @param name       name of the subpart relation
 * @param n          subpart being searched for is name_(n-1)
 * @param print      If print == 1, complain if the subpart is not found
 * @param maxParts   if name_(n-1) is found, maxParts returns the maximum number of
 *                   parts for the subclass branch the subpart is found in
 * @return   subpart if found, NULL otherwise
 */
Node* findPartDown(Node* node, const char* name, int n, int print, int* maxParts) {
   int i, p;
   TMLPart* part;
   TMLPart* tmp;
   Node* found;
   char* anonName;
   char anonArr[MAX_NAME_LENGTH];
   Name_and_Ptr* name_and_ptr;
   Node* subclNode;
   QNode* classToObjPtrsList;
   QNode* qnode;

   HASH_FIND_STR(node->cl->part, name, part);
   if (part != NULL) {
      if ((n != -1 && part->n >= n) || (n == -1 && part->n == 1)) {
         if (n == -1) n = 1;
         p = 0;
         HASH_ITER(hh, node->cl->part, part, tmp) {
            if (strcmp(part->name, name) == 0) break;
            p++;
         }
         *maxParts = part->maxNumParts;
         return node->part[p][n-1];
      } else if (n == -1 && part->n != 1) {
         if (print == 1)
            printf("%s has %d subparts with the subpart relation %s. Please specify which one using the PartName_# syntax (e.g., Adult_2).\n", node->name, part->n, name);
         return NULL;
      } else if (part->defaultPart == 0) {
         if (n == -1) n = 1;
         if (node->cl->par == NULL && print == 1) printf("%s does not have a %dth %s part.\n", node->name, n, name);
         return NULL;
      }
   }
   if (node->cl->nsubcls != 0) {
      if (node->assignedSubcl != -1 && node->subclMask == NULL) {
         found = findPartDown(node->subcl, name, n, print, maxParts);
         if (found != NULL) return found;
      } else {
         for (i = 0; i < node->cl->nsubcls; i++) {
            if (node->subclMask != NULL && node->subclMask[i] != 1) continue;
            if (part != NULL && part->defaultPartForSubcl[i] == 0) continue;
            found = findPartDown(&(node->subcl[i]), name, n, print, maxParts);
            if (found != NULL) return found;
         }
      }
      if (node->cl->par == NULL && print == 1) printf("%s does not have a %dth %s part.\n", node->name, n, name);
      return found;
   }
   if (n == -1) n = 1;
   if (node->cl->par == NULL && print == 1) printf("%s does not have a %dth %s part.\n", node->name, n, name);
   return NULL;
}

/**
 * Searches for a subpart name_(n-1). As the search continues, initialize nodes
 * along the path with anonymous path names created by anonStr
 *
 * @param kb         TML KB
 * @param node       current node
 * @param name       name of the subpart relation
 * @param n          we are looking for name_(n-1)
 * @param anonStr    name of pathname so far 
 */
Node* findAndInitPartDown(TMLKB* kb, Node* node, const char* name, int n, const char* anonStr) {
   int i, p;
   TMLPart* part;
   TMLPart* tmp;
   Node* found;
   char* anonName;
   char anonArr[MAX_NAME_LENGTH];
   Name_and_Ptr* name_and_ptr;
   Node* subclNode;
   QNode* classToObjPtrsList;
   QNode* qnode;

   HASH_FIND_STR(node->cl->part, name, part);
   if (part != NULL && part->n >= n) {
      p = 0;
      HASH_ITER(hh, node->cl->part, part, tmp) {
         if (strcmp(part->name, name) == 0) break;
         p++;
      }
      found = node->part[p][n-1];
      if (found == NULL) {
         sprintf(anonArr, "%s.%s[%d]", anonStr, part->name, n);
         anonName = strdup(anonArr);
         found = initAnonNodeToClass(part->cl, anonName, part->cl);
         addParent(found, node);
         node->part[p][n-1] = found;
      }
      return found;
   } else if (part != NULL && part->defaultPart == 0) return NULL;
   if (node->cl->nsubcls != 0) {
      if (node->subcl == NULL) {
         node->subcl = (Node*)malloc(sizeof(Node)*node->cl->nsubcls);
         for (i = 0; i < node->cl->nsubcls; i++) {
            node->subcl[i].name = NULL;
         }
         for (i = 0; i < node->cl->nsubcls; i++) {
            subclNode = &(node->subcl[i]);
            initializeNode(subclNode, node->cl->subcl[i], anonName);
            addParent(subclNode, node);
         }
      }
      if (node->assignedSubcl != -1 && node->subclMask == NULL) {
         found = findAndInitPartDown(kb, node->subcl, name, n, anonStr);
         if (found != NULL) return found;
      } else {
         for (i = 0; i < node->cl->nsubcls; i++) {
            if (node->subclMask != NULL && node->subclMask[i] != 1) continue;
            if (part != NULL && part->defaultPartForSubcl[i] == 0) continue;
            found = findAndInitPartDown(kb, &(node->subcl[i]), name, n, anonStr);
            if (found != NULL) return found;
         }
      }
   }
   return NULL;
}


void fillOutSubclasses(Node* node) {
   TMLClass* cl = node->cl;
   Node* subclNode;
   int i;

   if (node->subcl == NULL) {
      node->subcl = (Node*)malloc(sizeof(Node)*cl->nsubcls);
      for (i = 0; i < cl->nsubcls; i++) {
         node->subcl[i].name = NULL;
      }
   }
   if (node->assignedSubcl != -1) {
      if (node->subclMask == NULL) {
         if (node->subcl->name == NULL) {
            initializeNode(node->subcl, cl->subcl[node->assignedSubcl], node->name);
            addParent(node->subcl, node);
         }
         fillOutSubclasses(node->subcl);
         return;
      } else {
         if ((&(node->subcl[node->assignedSubcl]))->name == NULL) {
            initializeNode(&(node->subcl[node->assignedSubcl]), cl->subcl[node->assignedSubcl], node->name);
            addParent(&(node->subcl[node->assignedSubcl]), node);
         }
         fillOutSubclasses(&(node->subcl[node->assignedSubcl]));
      }
      return;
   }
   for (i = 0; i < cl->nsubcls; i++) {
      if (node->subclMask != NULL && node->subclMask[i] != 1) continue;
      subclNode = &(node->subcl[i]);
      if (subclNode->name == NULL) {
         initializeNode(subclNode, cl->subcl[i], node->name);
         addParent(subclNode, node);
      }
      fillOutSubclasses(subclNode);
   }
}

/**
 * Fill out an SPN to add in subpart objects that were never named
 *
 * @param kb         TML KB
 * @param node       node whose subpart and subclass nodes need to be created if anonymous
 * @param anonName   name of the node if it doesn't have one
 * @return weight of the world rooted at node
 */ 
float fillOutSPN(TMLKB* kb, Node* node, TMLClass* assignedClassBySuperpart, char* anonName) {
   int i, j;
   int c;
   float logZ = 0.0;
   float* subclZ;
   int n;
   TMLClass* cl = node->cl;
   Node** partNodes;
   Node* partNode;
   Node* subclNode;
   Node* tmpNode;
   char anonArr[MAX_LINE_LENGTH+1];
   char* newAnonName;   
   Name_and_Ptr* name_and_ptr;
   TMLRelation* rel;
   TMLRelation* tmp;
   TMLAttribute* attr;
   TMLAttribute* tmpa;
   TMLClass* tmpcl;
   TMLClass* partcl;
   TMLPart* part;
   TMLPart* tmppart;
   QNode* qnode;
   QNode* classToObjPtrsList;
   int maxParts = -1;
   int p;
   char** pn;
   Name_and_Ptr* traversal;
   int** relValues = node->relValues;
   int descendantIdx = isDescendant(assignedClassBySuperpart, node->cl);

   if (node->cl->par != NULL) {
      node->pathname = (*(node->par))->pathname;
   }
   if (node->changed == 0 && descendantIdx == -1) return node->logZ;
   if (node->cl->par == NULL) {
      HASH_FIND(hh_path, kb->objectPathToPtr, node->pathname, strlen(node->pathname), tmpNode);
      if (tmpNode == NULL)
         HASH_ADD_KEYPTR(hh_path, kb->objectPathToPtr, node->pathname, strlen(node->pathname), node);
   }
   if (node->cl->par == NULL || (*(node->par))->assignedSubcl != -1) {
      classToObjPtrsList = kb->classToObjPtrs[node->cl->id];
      qnode = (QNode*)malloc(sizeof(QNode));
      qnode->ptr = node;
      qnode->next = classToObjPtrsList;
      kb->classToObjPtrs[node->cl->id] = qnode;
   }


   // If not complete, fill out subclasses
   if (node->assignedSubcl == -1 && cl->nsubcls != 0 && node->subclMask == NULL) {
      subclZ = (float*)malloc(sizeof(float)*cl->nsubcls);
      for (i = 0; i < cl->nsubcls; i++) subclZ[i] = 0.0;
      i = 0;
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (rel->defaultRel == 0) {
            logZ += relWeight((*relValues), rel);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (rel->defaultRelForSubcl[j] == 0) {
                  subclZ[j] += relWeight((*relValues), rel);
               }
            }
         }
         i++;
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         if (attr->defaultAttr == 0) {
            logZ += attrWeight(node, attr);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (attr->defaultAttrForSubcl[j] == 0) {
                  subclZ[j] += attrWeight(node, attr);
               }
            }
         }
      }
      i = 0;
      HASH_ITER(hh, cl->part, part, tmppart) {
         maxParts = part->n;
            for (j = 0; j < part->n; j++) {
               partNode = node->part[i][j];
               if (partNode == NULL) {
                  if (cl->par != NULL) {
                     partNode = findPartUp(node, part->name, j, &maxParts);
                     if (partNode != NULL) {
                        node->part[i][j] = partNode;
                        if (partNode->pathname == NULL) {
                           sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                           partNode->pathname = strdup(anonArr);
                        }
                        if (part->defaultPart == 0)
                           logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                        else {
                           for (c = 0; c < cl->nsubcls; c++) {
                              if (part->defaultPartForSubcl[c] == 0)
                                 subclZ[c] += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                           }
                        }
                        continue;
                     }
                  }
                  sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                  newAnonName = strdup(anonArr);
                  partNode = initAnonNodeToClass(part->clOfOverriddenPart, newAnonName, part->clOfOverriddenPart);
                  fillOutSubclasses(partNode);
                  addParent(partNode, node);
                  subclNode = partNode;
                  node->part[i][j] = partNode;
                  if (node->cl->par != NULL)
                     propagatePartUp(*(node->par),partNode,part->name,j);
                  if (part->defaultPart == 0)
                     logZ += fillOutSPN(kb, partNode, part->cl, newAnonName);
                  else {
                     for (c = 0; c < cl->nsubcls; c++) {
                        if (part->defaultPartForSubcl[c] == 0)
                           subclZ[c] += fillOutSPN(kb, partNode, part->cl, newAnonName);
                     }
                  }
               } else {
                  if (partNode->pathname == NULL) {
                     sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                     partNode->pathname = strdup(anonArr);
                  }
                  if (part->defaultPart == 0)
                     logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                  else {
                     for (c = 0; c < cl->nsubcls; c++) {
                        if (part->defaultPartForSubcl[c] == 0)
                           subclZ[c] += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                     }
                  }
               }
            }
         i++;
      }
      if (node->subcl == NULL)
         fillOutSubclasses(node);
      if (descendantIdx != -1) {
         subclNode = &(node->subcl[descendantIdx]);
         logZ += cl->wt[descendantIdx]+fillOutSPN(kb, subclNode, assignedClassBySuperpart, anonName);
      } else {
         for (i = 0; i < cl->nsubcls; i++) {
            subclNode = &(node->subcl[i]);
            subclZ[i] += cl->wt[i]+fillOutSPN(kb, subclNode, assignedClassBySuperpart, anonName);
         }
         logZ += logsumarr_float(subclZ, cl->nsubcls);
      }
      free(subclZ);
   } else if (node->assignedSubcl == -1 && node->subclMask != NULL) {
      subclZ = (float*)malloc(sizeof(float)*cl->nsubcls);
      for (i = 0; i < cl->nsubcls; i++) subclZ[i] = 0.0;
      i = 0;
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (rel->defaultRel == 0) {
            logZ += relWeight((*relValues), rel);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (node->subclMask[j] == 1 && rel->defaultRelForSubcl[j] == 0) {
                  subclZ[j] += relWeight((*relValues), rel);
               }
            }
         }
         i++;
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         if (attr->defaultAttr == 0) {
            logZ += attrWeight(node, attr);
         } else {
            for (j = 0; j < cl->nsubcls; j++) {
               if (node->subclMask[j] == 1 && attr->defaultAttrForSubcl[j] == 0) {
                  subclZ[j] += attrWeight(node, attr);
               }
            }
         }
      }
      i = 0;
      HASH_ITER(hh, cl->part, part, tmppart) {
         maxParts = part->n;
            for (j = 0; j < part->n; j++) {
               partNode = node->part[i][j];
               if (partNode == NULL) {
                  if (cl->par != NULL) {
                     partNode = findPartUp(node, part->name, j, &maxParts);
                     if (partNode != NULL) {
                        node->part[i][j] = partNode;
                        if (partNode->pathname == NULL) {
                           sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                           partNode->pathname = strdup(anonArr);
                        }
                        if (part->defaultPart == 0)
                           logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                        else {
                           for (c = 0; c < cl->nsubcls; c++) {
                              if (part->defaultPartForSubcl[c] == 0)
                                 subclZ[c] += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                           }
                        }
                        continue;
                     }
                  }
                  sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                  newAnonName = strdup(anonArr);
                  partNode = initAnonNodeToClass(part->clOfOverriddenPart, newAnonName, part->clOfOverriddenPart);
                  fillOutSubclasses(partNode);
                  partNode->par = (Node**)malloc(sizeof(Node*));
                  *(partNode->par) = node;
                  partNode->npars = 1;
                  node->part[i][j] = partNode;
                  if (node->cl->par != NULL)
                     propagatePartUp(*(node->par),partNode,part->name,j);
                  if (part->defaultPart == 0)
                     logZ += fillOutSPN(kb, partNode, part->cl, newAnonName);
                  else {
                     for (c = 0; c < cl->nsubcls; c++) {
                        if (part->defaultPartForSubcl[c] == 0)
                           subclZ[c] += fillOutSPN(kb, partNode, part->cl, newAnonName);
                     }
                  }
               } else {
                  if (partNode->pathname == NULL) {
                     sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                     partNode->pathname = strdup(anonArr);
                  }
                  if (part->defaultPart == 0)
                     logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                  else {
                     for (c = 0; c < cl->nsubcls; c++) {
                        if (part->defaultPartForSubcl[c] == 0)
                           subclZ[c] += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                     }
                  }
               }
            }
         i++;
      }
      for (i = 0; i < cl->nsubcls; i++) {
         if (node->subclMask[i] != 1 || (descendantIdx != -1 && descendantIdx != i)) {
            subclZ[i] += log(0.0);
         } else {
            subclNode = &(node->subcl[i]);
            subclZ[i] += cl->wt[i]+fillOutSPN(kb, subclNode, assignedClassBySuperpart, anonName);
         }
      }
      logZ += logsumarr_float(subclZ, cl->nsubcls);
      free(subclZ);
   } else if (node->assignedSubcl != -1 && cl->nsubcls != 0) {
      i = 0;
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (rel->defaultRel == 0) {
            logZ += relWeight((*relValues), rel);
         } else {
            if (rel->defaultRelForSubcl[node->assignedSubcl] == 0) {
               logZ += relWeight((*relValues), rel);
            }
         }
         i++;
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         if (attr->defaultAttr == 0) {
            logZ += attrWeight(node, attr);
         } else {
            if (attr->defaultAttrForSubcl[node->assignedSubcl] == 0) {
               logZ += attrWeight(node, attr);
            }
         }
      }
      i = 0;
      HASH_ITER(hh, cl->part, part, tmppart) {
            maxParts = part->n;
            for (j = 0; j < part->n; j++) {
               partNode = node->part[i][j];
               if (partNode == NULL) {
                  if (cl->par != NULL) {
                     partNode = findPartUp(node, part->name, j, &maxParts);
                     if (partNode != NULL) {
                        node->part[i][j] = partNode;
                        if (partNode->pathname == NULL) {
                           sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                           partNode->pathname = strdup(anonArr);
                        }
                        if (part->defaultPart == 0 || part->defaultPartForSubcl[node->assignedSubcl] == 0)
                           logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                        continue;
                     }
                  }
                  sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                  newAnonName = strdup(anonArr);
                  partNode = initAnonNodeToClass(part->clOfOverriddenPart, newAnonName, part->clOfOverriddenPart);
                  fillOutSubclasses(partNode);
                  partNode->par = (Node**)malloc(sizeof(Node*));
                  *(partNode->par) = node;
                  partNode->npars = 1;
                  node->part[i][j] = partNode;
                  if (node->cl->par != NULL)
                     propagatePartUp(*(node->par),partNode,part->name,j);
                  if (part->defaultPart == 0 || part->defaultPartForSubcl[node->assignedSubcl] == 0)
                     logZ += fillOutSPN(kb, partNode, part->cl, newAnonName);
               } else {
                  if (partNode->pathname == NULL) {
                     sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                     partNode->pathname = strdup(anonArr);
                  }
                  if (part->defaultPart == 0 || part->defaultPartForSubcl[node->assignedSubcl] == 0)
                     logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
               }
            }
         i++;
      }
      if (node->subclMask == NULL)
         logZ += cl->wt[node->assignedSubcl]+fillOutSPN(kb, node->subcl, assignedClassBySuperpart, anonName);
      else
         logZ += cl->wt[node->assignedSubcl]+fillOutSPN(kb, &(node->subcl[node->assignedSubcl]), assignedClassBySuperpart, anonName);
   } else {
      i = 0;
      HASH_ITER(hh, cl->rel, rel, tmp) {
         logZ += relWeight((*relValues), rel);
         i++;
         relValues++;
      }
      HASH_ITER(hh, cl->attr, attr, tmpa) {
         logZ += attrWeight(node, attr);
      }
      i = 0;
      HASH_ITER(hh, cl->part, part, tmppart) {
         maxParts = part->n;
         for (j = 0; j < part->n; j++) {
            partNode = node->part[i][j];
            if (partNode == NULL) {
               if (cl->par != NULL) {
                  partNode = findPartUp(node, part->name, j, &maxParts);
                  if (partNode != NULL) {
                     node->part[i][j] = partNode;
                     if (partNode->pathname == NULL) {
                        sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                        partNode->pathname = strdup(anonArr);
                     }
                     logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
                     continue;
                  }
               }
               sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
               newAnonName = strdup(anonArr);
               partNode = initAnonNodeToClass(part->clOfOverriddenPart, newAnonName, part->clOfOverriddenPart);
               fillOutSubclasses(partNode);
               partNode->par = (Node**)malloc(sizeof(Node*));
               *(partNode->par) = node;
               partNode->npars = 1;
               node->part[i][j] = partNode;
               if (node->cl->par != NULL)
                  propagatePartUp(*(node->par),partNode,part->name,j);
               logZ += fillOutSPN(kb, partNode, part->cl, newAnonName);
            } else {
               if (partNode->pathname == NULL) {
                  sprintf(anonArr, "%s.%s[%d]", anonName, part->name, (j+1));
                  partNode->pathname = strdup(anonArr);
               }
               logZ += fillOutSPN(kb, partNode, part->cl, partNode->pathname);
            }
         }
         i++;
      }   
   }
   node->logZ = logZ;
   node->changed = 0;
   return logZ;
}


Node* addClassEvidenceForObj(TMLKB* kb, char* objectName, Node* node, char* className, int pol,
   FILE* tmlFactFile, int linenum) {
   Name_and_Ptr* name_and_ptr;
   TMLClass* cl;
   Node* new_node = NULL;
   Node* par;

   HASH_FIND_STR(kb->classNameToPtr, className, cl);
   if (cl == NULL) {
      if (tmlFactFile != NULL) {
         printf("Error on line %d in fact file: Unknown class %s.\n", linenum, className);
         return NULL;
      }
      printf("Unknown class %s.\n", className);
      return NULL;
   }
   if (pol == 0)
      new_node = blockClassForNode(kb, NULL, objectName, node, cl, tmlFactFile, linenum);
   else
      new_node = updateClassForNode(kb, NULL, objectName, node, cl, tmlFactFile, linenum);

   // Check that new class information does not block overriding subpart relations
   par = node;
   while (par->cl->par != NULL) par = *(par->par);
   if (par->npars != 0) {
      par = *(par->par);
      blockClassesForPartQuery(NULL, par, node, cl);
   }

   return new_node;
}

/**
 * Determine type of a line in an object description based on syntax
 */
int determineObjectLineType(TMLKB* kb, TMLClass* cl, char* line) {
   char cl_fmt_str[50];
   char subpart_fmt_str[50];
   char str1[MAX_NAME_LENGTH+1];
   char str2[MAX_NAME_LENGTH+1];
   int correctScan;
   TMLRelation* rel;
   TMLClass* tmpcl;
   TMLPart* part;

   if(strchr(line, '(') != NULL) return RELATION; // 2 = relation

   snprintf(subpart_fmt_str, 50, " %%%d[^, \t\n\r\v\f] %%%d[^, \t\r\n\v\f]", MAX_NAME_LENGTH, MAX_NAME_LENGTH);

   correctScan = sscanf(line, subpart_fmt_str, str1, str2);
   if (correctScan == 2) {
      if (strchr(line, '[') != NULL) return SUBPART; // 1 = subpart
      tmpcl = cl;
      while (tmpcl != NULL) {
         part = getPart(tmpcl, str1);
         if (part != NULL) return SUBPART; // subpart
         tmpcl = tmpcl->par;
      }
      return ATTRIBUTE; // attribute 
   } else if (correctScan == 1) {
      if (str1[0] == '!') HASH_FIND_STR(kb->classNameToPtr, str1+1, tmpcl);
      else HASH_FIND_STR(kb->classNameToPtr, str1, tmpcl);
      if (tmpcl != NULL) return SUBCLASS; // subclass
      tmpcl = cl;
      while (tmpcl != NULL) {
         rel = getRelation(tmpcl, str1);
         if (rel != NULL) return RELATION; // relation
         tmpcl = tmpcl->par;
      }
      return ERROR;
   } else {
      return EMPTY_LINE; // empty line
   }
}

void readInTMLFacts(TMLKB* kb, const char* tmlFactFileName) {
   FILE* tmlFactFile = fopen(tmlFactFileName, "r");
   char objectName[MAX_NAME_LENGTH+1];
   char tmpline[MAX_LINE_LENGTH+1];
   char className[MAX_NAME_LENGTH+1];
   char keyword[MAX_NAME_LENGTH+1];
   int linenum = 0;
   char* line;
   char* restOfLine = NULL;
   int correctScan;
   char obj_fmt_str[50];
   char part_fmt_str[50];
   char part_idx_fmt_str[50];
   char rel_fmt_str[50];
   char rel_no_args_fmt_str[50];
   char rel_no_args2_fmt_str[50];
   char keyword_fmt_str[15];
   char comma[2];
   char name[MAX_NAME_LENGTH+1];
   int numObjects = 0;
   int o = 0;

   TMLClass* cl;
   char* objName;
   Node* node;
   char* iter;
   Name_and_Ptr* clPtr;
   TMLClass* tmpcl;
   QNode* obj;
   Node* output;

   if (tmlFactFile == NULL) {
      sprintf(objectName, "%s.db", tmlFactFileName);
      tmlFactFile = fopen(objectName, "r");
      if (tmlFactFile == NULL) {
         printf("Error. Cannot find fact file named %s.\n", tmlFactFileName);
         freeTMLKB(kb);
         exit(1);
      }
   }
   snprintf(obj_fmt_str, 50, "%%%d[a-zA-Z0-9:] %%%d[]a-zA-Z0-9:[.] %%1[{]", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(part_fmt_str, 50, " %%%d[a-zA-Z0-9:] %%%d[a-zA-Z0-9:] %%1[,]", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(part_idx_fmt_str, 50, " %%%d[a-zA-Z0-9:][%%d] %%%d[_a-zA-Z0-9:] %%1[,]", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(rel_fmt_str, 50, " %%%d[!a-zA-Z0-9:](%%%d[][a-zA-Z0-9:,]) %%1[,]", MAX_NAME_LENGTH, MAX_LINE_LENGTH);
   snprintf(rel_no_args_fmt_str, 50, " %%%d[!a-zA-Z0-9:]( ) %%1[,]", MAX_NAME_LENGTH);
   snprintf(rel_no_args2_fmt_str, 50, " %%%d[!a-zA-Z0-9:] %%1[,]", MAX_NAME_LENGTH);
   snprintf(keyword_fmt_str, 15, " %%%ds", MAX_NAME_LENGTH);

   // Count the named objects
   while (fgets(tmpline, MAX_LINE_LENGTH, tmlFactFile) != NULL) {
      if (strchr(tmpline, '{') != NULL) numObjects++;
   }
   rewind(tmlFactFile);

   line = getLineToSemicolon(tmlFactFile, &restOfLine, &linenum);
   if (line == NULL) {
      objName = strdup("TopObject");
      node = initNodeToClass(kb, objName, kb->topcl, kb->topcl);
      kb->root = (Name_and_Ptr*)malloc(sizeof(Name_and_Ptr));
      kb->root->name = objName;
      node->pathname = strdup(objName);
      kb->root->ptr  = node;
      fclose(tmlFactFile);
      return;
   }
   while (line != NULL) {
      // read in object name and class
      correctScan = sscanf(line, obj_fmt_str, className, objectName, comma);
      if (correctScan != 3) {
         printf("Error on line \"%s\" in fact file: Expected \" ObjectClass ObjectName {\"\n", line);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      HASH_FIND_STR(kb->classNameToPtr, objectName, cl);
      if (cl != NULL) {
         printf("Error in fact file: %s is already the name of a class.\n", objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      HASH_FIND_STR(kb->classNameToPtr, className, cl);
      if (cl == NULL) {
         printf("Error in fact file: %s is not the name of a class, but is named as the class for object %s.\n", className, objectName);
         free(line);
         if (restOfLine != NULL) free(restOfLine);
         freeTMLKB(kb);
         fclose(tmlFactFile);
         exit(1);
      }
      HASH_FIND_STR(kb->objectNameToPtr, objectName, node);
      if (node == NULL) {
         if (strchr(objectName, '.') == NULL) {
            if (strpbrk(objectName, "0123456789") == objectName) {
               printf("Error in fact file: Object names must begin with a letter. %s does not.\n", objectName);
               free(line);
               if (restOfLine != NULL) free(restOfLine);
               freeTMLKB(kb);
               fclose(tmlFactFile);
               exit(1);
            }
            if (cl == kb->topcl) {
               if (kb->root != NULL) {
                  printf("Error in fact file. Multiple objects were designated with as the TopClass. Only one object can be of this class.\n");
                  free(line);
                  if (restOfLine != NULL) free(restOfLine);
                  freeTMLKB(kb);
                  fclose(tmlFactFile);
                  exit(1);
               }
               if (strchr(objectName, '.') != NULL) {
                  printf("The top object %s should not have a pathname.\n", objectName);
                  free(line);
                  if (restOfLine != NULL) free(restOfLine);
                  freeTMLKB(kb);
                  fclose(tmlFactFile);
                  exit(1);
               }
               kb->root = (Name_and_Ptr*)malloc(sizeof(Name_and_Ptr));
               kb->root->name = strdup(objectName);
               node = initNodeToClass(kb, kb->root->name, kb->topcl, kb->topcl);
               node->pathname = strdup(kb->root->name);
               kb->root->ptr  = node;
            } else {
               //found a node that is not the top node but isn't a subpart yet. will check later
               node = initNodeToClass(kb, strdup(objectName), cl, cl);
               node->pathname = NULL;
               tmpcl = rootClass(node->cl);
            }
         } else {
            node = findNodeFromAnonName(kb, NULL, objectName, 1);
            if (node == NULL) {
               printf("Error in fact file: Pathname %s is unknown for the TML KB.\n", objectName);
               free(line);
               if (restOfLine != NULL) free(restOfLine);
               freeTMLKB(kb);
               fclose(tmlFactFile);
               exit(1);
            }
         }
      } else {
         output = addClassEvidenceForObj(kb, (node->name == NULL ? node->pathname : node->name), node, className, 1, tmlFactFile, linenum);
         if (output == NULL) {
            free(line);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlFactFile);
            exit(1);
         }
      }
      linenum = readInOneObject(kb, objectName, node, cl, tmlFactFile, linenum);
      free(line);
      line = getLineToSemicolon(tmlFactFile, &restOfLine, &linenum);
      o++;
   }
   if (o != numObjects) {
      printf("Error on line %d of fact file.\n", linenum);
      if (restOfLine != NULL) free(restOfLine);
      freeTMLKB(kb);
      fclose(tmlFactFile);
      exit(1);
   }
}

void readInTMLRules(TMLKB* kb, const char* tmlRuleFileName) {
   FILE* tmlRuleFile = fopen(tmlRuleFileName, "r");
   char line[MAX_LINE_LENGTH+1];
   char lineType[MAX_NAME_LENGTH+1];
   char className[MAX_NAME_LENGTH+1];
   int linenum = 0;
   char* fullline;
   char* restOfLine = NULL;
   int correctScan;
   int numClasses = 0;
   int id = 0;
   TMLClass* cl;
   int i;
   Name_and_Ptr* name_and_ptr;
   TMLClass* rootCl;
   QNode* rootQueue = NULL;
   QNode* root;
   int lost;
   char cl_fmt_str[50];
   char bracket[2];

   if (tmlRuleFile == NULL) {
      sprintf(lineType, "%s.tml", tmlRuleFileName);
      tmlRuleFile = fopen(lineType, "r");
      if (tmlRuleFile == NULL) {
         printf("Error. Cannot find rule file named %s.\n", tmlRuleFileName);
         exit(1);
      }
   }

   // Count the classes
   while (fgets(line, MAX_LINE_LENGTH, tmlRuleFile) != NULL) {
      if (strchr(line, '{') != NULL) numClasses++;
   }
   rewind(tmlRuleFile);
   linenum = 0;
   kb->classes = (TMLClass*)malloc(sizeof(TMLClass)*numClasses);
   kb->numClasses = numClasses;
   kb->classToObjPtrs = (QNode**)malloc(sizeof(QNode*)*numClasses);
   for (i = 0; i < numClasses; i++) {
      kb->classToObjPtrs[i] = NULL;
   }

   numClasses = 0;
   snprintf(cl_fmt_str, 50, " class %%%d[a-zA-Z0-9._:] %%1[{]", MAX_NAME_LENGTH);
   fullline = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
   while (fullline != NULL) {
      if (strchr(fullline, '{') != NULL) {
         correctScan = sscanf(fullline, cl_fmt_str, className, bracket);
         if (correctScan != 2) {
            printf("Error on line %d in rule file: Expected \" ClassName {\"\n", linenum);
            free(fullline);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
         if (strpbrk(className, "0123456789") == className) {
            printf("Error on line %d in rule file: Class names must begin with a letter. %s does not.\n", linenum, className);
            free(fullline);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
         HASH_FIND_STR(kb->classNameToPtr, className, cl);
         if (cl != NULL) {
            printf("Error in rule file: A class named %s was declared twice.\n", className);
            free(fullline);
            if (restOfLine != NULL) free(restOfLine);
            freeTMLKB(kb);
            fclose(tmlRuleFile);
            exit(1);
         }
         cl = &(kb->classes[numClasses]);
         cl->id = numClasses;
         setUpTMLClass(cl, strdup(className), -1);
         HASH_ADD_KEYPTR(hh, kb->classNameToPtr, cl->name, strlen(cl->name), cl);
         numClasses++;
      }
      linenum++;
      free(fullline);
      fullline = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
   }
   if (numClasses != kb->numClasses) {
      if (restOfLine != NULL) free(restOfLine);
      freeTMLKB(kb);
      fclose(tmlRuleFile);
      exit(1);
   }
   rewind(tmlRuleFile);
   linenum = 0;

      //Read in TML rules
   fullline = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
   while (fullline != NULL) {
      correctScan = sscanf(fullline, cl_fmt_str, className, bracket);
      if (correctScan != 2) {
         printf("Error on line %d in rule file: Malformed rule file. Expected \"ClassName {\".\n", linenum);
         free(fullline);
         freeTMLKB(kb);
         fclose(tmlRuleFile);
         exit(1);
      }
      linenum++;
      rootCl = NULL;
      linenum = readInOneTMLClass(kb, className, &rootCl, tmlRuleFile, linenum, id, 0, 1);
      if (rootCl != NULL) {
         root = (QNode*)malloc(sizeof(QNode));
         root->ptr = rootCl;
         root->next = rootQueue;
         rootQueue = root;
      }
      id++;
      free(fullline);
      fullline = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
   }
   rewind(tmlRuleFile);
   fullline = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
   while (fullline != NULL) {
      correctScan = sscanf(fullline, cl_fmt_str, className, bracket);
      if (correctScan != 2) {
         printf("Error on line %d in rule file: Malformed rule file. Expected \"ClassName {\".\n", linenum);
         free(fullline);
         freeTMLKB(kb);
         fclose(tmlRuleFile);
         exit(1);
      }
      linenum++;
      rootCl = NULL;
      linenum = readInOneTMLClass(kb, className, &rootCl, tmlRuleFile, linenum, id, 0, 0);
      id++;
      free(fullline);
      fullline = getLineToSemicolon(tmlRuleFile, &restOfLine, &linenum);
   }
   root = rootQueue;
   while (root != NULL) {
      lost = checkForLostRoot((TMLClass*)(root->ptr));
      if (lost == 0) {
         printf("Error in rule file: Class %s is not a subclass and is not a subpart (or has a descendant that is a subpart) of any other class are specified before. There can only be one top class. (Note: Ancestors of a class MUST be declared first.)\n", ((TMLClass*)(root->ptr))->name);
         rootQueue = root->next;
         free(root);
         while (rootQueue != NULL) {
            root = rootQueue;
            rootQueue = root->next;
            free(root);
         }
         freeTMLKB(kb);
         fclose(tmlRuleFile);
         exit(1);
      }
      rootQueue = root->next;
      free(root);
      root = rootQueue;
   }
   rewind(tmlRuleFile);
}

/**
 * Returns top class in the KB
 */
TMLClass* getTopClass(TMLKB* kb) {
   return kb->topcl;
}

/**
 * Sets up a class in the kb
 */
TMLClass* addClass(TMLKB* kb, char* className, int id, int subclIdx) {
   TMLClass* cl = &(kb->classes[id]);
   setUpTMLClass(cl, className, subclIdx);
   return cl;
}

/**
 * Adds a relation fact relStr(obj,...) of polarity pol to all
 * reachable SPN nodes 
 */
void addRelationToKB(TMLKB* kb, Node* obj, const char* relStr, int pol) {
   TMLClass* cl = obj->cl;
   TMLRelation* foundrel = getRelation(cl, relStr);
   int r = 0;
   int c;
   TMLRelation* rel;
   TMLRelation* tmp;
   Node* subcl = obj->subcl;

   if (foundrel != NULL) {
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (foundrel == rel) break;
         r++;
      }
      if (pol == 0) {
         obj->relValues[r][0]++;
         obj->relValues[r][2]--;
      } else {
         obj->relValues[r][1]++;
         obj->relValues[r][2]--;
      }
      if (kb != NULL)
         kb->edits = addKBEdit(obj, -1, NULL, r, -1, pol, kb->edits);
      if (foundrel->defaultRel == 0) return;
   }
   if (cl->nsubcls == 0) return;
   if (obj->assignedSubcl != -1) {
      if (obj->subclMask == NULL)
         addRelationToKB(kb, subcl, relStr, pol);
      else
         addRelationToKB(kb, &(subcl[obj->assignedSubcl]), relStr, pol);
   } else if (obj->subclMask == NULL) {
      for (c = 0; c < cl->nsubcls; c++) {
         addRelationToKB(kb, &(subcl[c]), relStr, pol);
      }
   } else {
      for (c = 0; c < cl->nsubcls; c++) {
         if ((foundrel == NULL || foundrel->defaultRelForSubcl==NULL || foundrel->defaultRelForSubcl[c] == 1) &&
               (obj->subclMask != NULL && obj->subclMask[c] == 1))
            addRelationToKB(kb, subcl, relStr, pol);
         subcl++;
      } 
   }
}

/**
 * Adds an attribute fact Attribute(Object,Value) of polarity pol to all
 * reachable SPN nodes 
 */
void addAttributeToKB(TMLKB* kb, Node* obj, const char* attrStr, const char* attrvalStr, int pol) {
   TMLClass* cl = obj->cl;
   TMLAttribute* attr = getAttribute(cl, attrStr);
   int i, c;
   TMLAttrValue* attrval;
   Node* subcl = obj->subcl;

   if (attr != NULL) {
      HASH_FIND_STR(attr->vals, attrvalStr, attrval);
      if (attrval != NULL) {
         if (pol == 0) {
            if (obj->attrValues[attr->idx] == NULL) {
               obj->attrValues[attr->idx] = (int*)malloc(sizeof(int)*attr->nvals);
               for (i = 0; i < attr->nvals; i++)
                  obj->attrValues[attr->idx][i] = 0;
            }
            obj->attrValues[attr->idx][attrval->idx] = -1;
         } else {
            obj->assignedAttr[attr->idx] = attrval;
         }
         if (kb != NULL)
            kb->edits = addKBEdit(obj, 0, NULL, attr->idx, attrval->idx, pol, kb->edits);
      }
      if (attr->defaultAttr == 0) return;
   }
   if (cl->nsubcls == 0) return;
   if (obj->assignedSubcl != -1) {
      if (obj->subclMask == NULL)
         addAttributeToKB(kb, subcl, attrStr, attrvalStr, pol);
      else
         addAttributeToKB(kb, &(subcl[obj->assignedSubcl]), attrStr, attrvalStr, pol);
   } else if (obj->subclMask == NULL) {
      for (c = 0; c < cl->nsubcls; c++) {
         addAttributeToKB(kb, &(subcl[c]), attrStr, attrvalStr, pol);
      }
   } else {
      for (c = 0; c < cl->nsubcls; c++) {
         if ((attr == NULL || attr->defaultAttrForSubcl == NULL || attr->defaultAttrForSubcl[c] == 1) &&
               (obj->subclMask != NULL && obj->subclMask[c] == 1))
            addAttributeToKB(kb, subcl, attrStr, attrvalStr, pol);
         subcl++;
      }
   }
}

/**
 * Adds an attribute fact Attribute(Object,Value) of polarity pol to all
 * reachable SPN nodes 
 */
void removeAttributeToKB(Node* obj, const char* attrStr, const char* attrvalStr, int pol) {
   TMLClass* cl = obj->cl;
   TMLAttribute* attr = getAttribute(cl, attrStr);
   int i, c;
   TMLAttrValue* attrval;
   Node* subcl = obj->subcl;

   if (attr != NULL) {
      HASH_FIND_STR(attr->vals, attrvalStr, attrval);
      if (attrval != NULL) {
         if (pol == 0) {
            obj->attrValues[attr->idx][attrval->idx] = 0;
         } else {
            obj->assignedAttr[attr->idx] = NULL;
         }
      }
      if (attr->defaultAttr == 0) return;
   }
   if (cl->nsubcls == 0) return;
   if (obj->assignedSubcl != -1) {
      if (obj->subclMask == NULL)
         removeAttributeToKB(subcl, attrStr, attrvalStr, pol);
      else
         removeAttributeToKB(&(subcl[obj->assignedSubcl]), attrStr, attrvalStr, pol);
   } else if (obj->subclMask == NULL) {
      for (c = 0; c < cl->nsubcls; c++) {
         removeAttributeToKB(&(subcl[c]), attrStr, attrvalStr, pol);
      }
   } else {
      for (c = 0; c < cl->nsubcls; c++) {
         if ((attr == NULL || attr->defaultAttrForSubcl == NULL || attr->defaultAttrForSubcl[c] == 1) &&
               (obj->subclMask != NULL && obj->subclMask[c] == 1))
            removeAttributeToKB(subcl, attrStr, attrvalStr, pol);
         subcl++;
      }
   }
}

/**
 * Removes a relation fact relStr(obj,...) of polarity pol from
 * all reachable SPN nodes
 */
void removeRelationToKB(Node* obj, const char* relStr, int pol) {
   TMLClass* cl = obj->cl;
   TMLRelation* foundrel = getRelation(cl, relStr);
   int r = 0;
   int c;
   TMLRelation* rel;
   TMLRelation* tmp;
   Node* subcl = obj->subcl;

   if (foundrel != NULL) {
      HASH_ITER(hh, cl->rel, rel, tmp) {
         if (foundrel == rel) break;
         r++;
      }
      if (pol == 0) {
         obj->relValues[r][0]--;
         obj->relValues[r][2]++;
      } else {
         obj->relValues[r][1]--;
         obj->relValues[r][2]++;
      }
      if (foundrel->defaultRel == 0) return;
   }
   if (cl->nsubcls == 0) return;
   if (obj->assignedSubcl != -1) {
      if (obj->subclMask == NULL)
         removeRelationToKB(subcl, relStr, pol);
      else
         removeRelationToKB(&(subcl[obj->assignedSubcl]), relStr, pol);
   } else if (obj->subclMask == NULL) {
      for (c = 0; c < cl->nsubcls; c++) {
         removeRelationToKB(&(subcl[c]), relStr, pol);
      }
   } else {
      for (c = 0; c < cl->nsubcls; c++) {
         if ((foundrel == NULL || foundrel->defaultRelForSubcl==NULL || foundrel->defaultRelForSubcl[c] == 1) &&
               (obj->subclMask != NULL && obj->subclMask[c] == 1))
            removeRelationToKB(subcl, relStr, pol);
         subcl++;
      } 
   }
}

/**
 * Blocks classes based on the existence of a subpart of node->name
 * called part->name_n
 */
int blockClassesForPartQueryRec(KBEdit** editsPtr, Node* node, TMLPart* part, int n, TMLClass* newClass) {
   int c;
   KBEdit* edits = editsPtr != NULL ? *editsPtr : NULL;
   TMLClass* cl = node->cl;
   TMLPart* foundPart = getPart(cl, part->name);
   int blockDefault = 0;
   int allSubclassesBlocked = 1;
   int block;

   if (foundPart != NULL) {
      if (foundPart->n <= n) blockDefault = 1;
      if (newClass != NULL && !isAncestor(foundPart->cl, newClass) && !isAncestor(newClass, foundPart->cl)) blockDefault = 1;
      if (foundPart->defaultPart == 0) {
         return blockDefault;
      }
      if (node->subclMask != NULL) {
         for (c = 0; c < cl->nsubcls; c++) {
            if (node->subclMask[c] == 0) continue;
            if (foundPart->defaultPartForSubcl[c] == 0) {
               if (blockDefault == 1) {
                  if (node->subclMask[c] == 1) node->subclMask[c] = -1;
                  else if (node->subclMask[c] < 0) node->subclMask[c]--;
                  if (editsPtr != NULL) edits = addKBEdit(node, c, NULL, -1, -1, 0, edits);
               }
               continue;
            }
            block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), &(node->subcl[c]), part, n, newClass);
            if (block == 1) {
               if (node->subclMask[c] == 1) node->subclMask[c] = -1;
               else if (node->subclMask[c] < 0) node->subclMask[c]--;
               edits = addKBEdit(node, c, NULL, -1, -1, 0, edits);
            }
            else allSubclassesBlocked = 0;
         }
         if (editsPtr != NULL)
            *editsPtr = edits;
         return (blockDefault == 1 && allSubclassesBlocked == 1);
      } else if (node->cl->nsubcls != 0) {
         node->subclMask = (int*)malloc(sizeof(int)*cl->nsubcls);
         for (c = 0; c < cl->nsubcls; c++) {
            if (foundPart->defaultPartForSubcl[c] == 0) {
               if (blockDefault == 1) {
                  if (node->subclMask[c] == 1) node->subclMask[c] = -1;
                  else if (node->subclMask[c] < 0) node->subclMask[c]--;
                  if (editsPtr != NULL) edits = addKBEdit(node, c, NULL, -1, -1, 0, edits);
               } else
                  node->subclMask[c] = 1;
               continue;
            }
            block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), &(node->subcl[c]), part, n, newClass);
            if (block == 1) {
               if (node->subclMask[c] == 1) node->subclMask[c] = -1;
               else if (node->subclMask[c] < 0) node->subclMask[c]--;
               if (editsPtr != NULL) edits = addKBEdit(node, c, NULL, -1, -1, 0, edits);
            } else {
               node->subclMask[c] = 1;
               allSubclassesBlocked = 0;
            }
         }
         if (editsPtr != NULL)
            *editsPtr = edits;
         return (blockDefault == 1 && allSubclassesBlocked == 1);
      }
   } else {
      if (node->subclMask != NULL) {
         for (c = 0; c < cl->nsubcls; c++) {
            if (node->subclMask[c] == 0) continue;
            block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), &(node->subcl[c]), part, n, newClass);
            if (block == 1) {
               if (node->subclMask[c] == 1) node->subclMask[c] = -1;
               else if (node->subclMask[c] < 0) node->subclMask[c]--;
               if (editsPtr != NULL) edits = addKBEdit(node, c, NULL, -1, -1, 0, edits);
            } else allSubclassesBlocked = 0;
         }
      } else if (node->cl->nsubcls != 0) {
         node->subclMask = (int*)malloc(sizeof(int)*cl->nsubcls);
         for (c = 0; c < cl->nsubcls; c++) {
            block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), &(node->subcl[c]), part, n, newClass);
            if (block == 1) {
               if (node->subclMask[c] == 1) node->subclMask[c] = -1;
               else if (node->subclMask[c] < 0) node->subclMask[c]--;
               if (editsPtr != NULL) edits = addKBEdit(node, c, NULL, -1, -1, 0, edits);
            } else {
               node->subclMask[c] = 1;
               allSubclassesBlocked = 0;
            }
         }
      }
      if (editsPtr != NULL)
         *editsPtr = edits;
      return (allSubclassesBlocked == 1);
   }
   
}

Node* blockClassesForPartQuery(KBEdit** editsPtr, Node* par, Node* subpart, TMLClass* newClass) {
   int c, p, n;
   int partIdx;
   KBEdit* edits = (editsPtr == NULL ? NULL : *editsPtr);
   Node* prevobj;
   Node* newobj;
   TMLPart* part;
   TMLPart* tmp;
   int block;
   int somethingBlocked = 0;
   Node* subcl;

   p = 0;
   HASH_ITER(hh, par->cl->part, part, tmp) {
      for (n = 0; n < part->n; n++) {
         if (par->part[p][n] == subpart) break;
      }
      if (n != part->n) break;
      p++;
   }
   if (part->defaultPart != 0 && par->assignedSubcl == -1) {
      subcl = par->subcl;
      if (par->subclMask != NULL) {
         for (c = 0; c < par->cl->nsubcls; c++) {
            if (par->subclMask[c] == 0 || part->defaultPartForSubcl[c] == 0) {
               subcl++; continue;
            }
            block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), subcl, part, n, newClass);
            if (block == 1) {
               if (par->subclMask[c] == 1) par->subclMask[c] = -1;
               else if (par->subclMask[c] < 0) par->subclMask[c]--;
               if (editsPtr != NULL) edits = addKBEdit(par, c, NULL, -1, -1, 0, edits);
               somethingBlocked = 1;
            }
            subcl++;
         }
      } else if (par->cl->nsubcls != 0) {
         if (par->subclMask == NULL) {
            par->subclMask = (int*)malloc(sizeof(int)*par->cl->nsubcls);
            for (c = 0; c < par->cl->nsubcls; c++) {
               par->subclMask[c] = 1;
            }
         }
         for (c = 0; c < par->cl->nsubcls; c++) {
            if (part->defaultPartForSubcl[c] == 0) {
               par->subclMask[c] = 1;
               subcl++;
               continue;
            }
            block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), subcl, part, n, newClass);
            if (block == 1) {
               if (par->subclMask[c] == 1) par->subclMask[c] = -1;
               else if (par->subclMask[c] < 0) par->subclMask[c]--;
               if (editsPtr != NULL) edits = addKBEdit(par, c, NULL, -1, -1, 0, edits);
               somethingBlocked = 1;
            } else par->subclMask[c] = 1;
            subcl++;
         }
      }
   }

   if (par->cl->par != NULL) {
      prevobj = par;
      newobj = *(par->par);

      while (newobj != NULL) {
         if (newobj->assignedSubcl != -1) break;
         subcl = newobj->subcl;
         if (newobj->subclMask != NULL) {
            for (c = 0; c < newobj->cl->nsubcls; c++) {
               if (subcl == prevobj) {
                  subcl++;
                  continue;
               }
               block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), subcl, part, n, newClass);
               if (block == 1) {
                  if (newobj->subclMask[c] == 1) newobj->subclMask[c] = -1;
                  else if (newobj->subclMask[c] < 0) newobj->subclMask[c]--;
                  if (editsPtr != NULL) edits = addKBEdit(newobj, c, NULL, -1, -1, 0, edits);
                  somethingBlocked = 1;
                  par = newobj;
               }
               subcl++;
            }
         } else if (newobj->cl->nsubcls != 0) {
            if (newobj->subclMask == NULL) {
               newobj->subclMask = (int*)malloc(sizeof(int)*newobj->cl->nsubcls);
               for (c = 0; c < newobj->cl->nsubcls; c++) {
                  newobj->subclMask[c] = 1;
               }
            }
            for (c = 0; c < newobj->cl->nsubcls; c++) {
               if (subcl == prevobj) {
                  newobj->subclMask[c] = 1;
                  subcl++;
                  continue;
               }
               block = blockClassesForPartQueryRec((editsPtr != NULL ? &edits : NULL), subcl, part, n, newClass);
               if (block == 1) {
                  if (newobj->subclMask[c] == 1) newobj->subclMask[c] = -1;
                  else if (newobj->subclMask[c] < 0) newobj->subclMask[c]--;
                  if (editsPtr != NULL) edits = addKBEdit(newobj, c, NULL, -1, -1, 0, edits);
                  somethingBlocked = 1;
                  par = newobj;
               } else {
                  newobj->subclMask[c] = 1;
               }
               subcl++;
            }
         }
         if (newobj->cl->par == NULL) break;
         prevobj = newobj;
         newobj = *(newobj->par);
      }
   }
   if (editsPtr != NULL)
      *editsPtr = edits;
   if (somethingBlocked == 1) {
      return par;
   }
   return NULL;
}

void computeAttributeQueryOrAddEvidenceForObj(TMLKB* kb, Node* obj, TMLAttribute* attr, TMLAttrValue* attrval, int pol, float logZ, int isQuery, int isMultQuery, FILE* outFile) {
   char* best = NULL;
   TMLClass* cl;
   float newLogZ;
   char* name;
   int correctScan;
   TMLAttrValue* tmpav;

   if (obj->name != NULL) {
      name = obj->name;
   } else {
      best = createBestPathname(kb, obj);
      name = best;
   }
   if (attrval == NULL) { // check probability for all values
      HASH_ITER(hh, attr->vals, attrval, tmpav) {
         computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, attrval, pol, logZ, isQuery, isMultQuery, outFile);
      }
   } else {
      if (obj->assignedAttr[attr->idx] != NULL) {
         if (obj->assignedAttr[attr->idx] == attrval) {
            if (isQuery) {
               if (isMultQuery == 1) {
                  printf("P[%s(%s,%s)] = 1.0\n", attr->name, name, attrval->name);
                  if (outFile != NULL)
                     fprintf(outFile, "P[%s(%s,%s)] = 1.0\n", attr->name, name, attrval->name);
               } else {
                  printf("Yes.\n");
                  if (outFile != NULL)
                     fprintf(outFile, "Yes.\n");
               }
            } else {
               printf("%s is already the assigned value of attribute %s for object %s.\n", attrval->name, attr->name, name);
               if (outFile != NULL)
                  fprintf(outFile, "%s is already the assigned value of attribute %s for object %s.\n", attrval->name, attr->name, name);
            }
            if (best != NULL) free(best);
            return;
         } else {
            if (isQuery) {
               if (isMultQuery == 1) {
                  printf("P[%s(%s,%s)] = 0.0\n", attr->name, name, attrval->name);
                  if (outFile != NULL)
                     fprintf(outFile, "P[%s(%s,%s)] = 0.0\n", attr->name, name, attrval->name);
               } else {
                  printf("No.\n");
                  if (outFile != NULL)
                     fprintf(outFile, "No.\n");
               }
            } else {
               printf("%s is already the assigned value of attribute %s for object %s. Attribute values are mutually exclusive.\n", obj->assignedAttr[attr->idx]->name, attr->name, name);
              if (outFile != NULL)
                  fprintf(outFile, "%s is already the assigned value of attribute %s for object %s. Attribute values are mutually exclusive.\n", obj->assignedAttr[attr->idx]->name, attr->name, name);
            }
         }
         if (best != NULL) free(best);
         return;
      } else if (obj->attrValues[attr->idx] != NULL && obj->attrValues[attr->idx][attrval->idx] < 0) {
         if (isMultQuery == 1) {
            if (best != NULL) free(best);
            return;
         }
         if (isQuery) {
            printf("No. Object %s is defined to not have %s as the value for attribute %s.\n", name, attrval->name, attr->name);
            if (outFile != NULL)
               fprintf(outFile, "No. Object %s is defined to not have %s as the value for attribute %s.\n", name, attrval->name, attr->name);
         } else {
            printf("Object %s is already defined to not have %s as the value for attribute %s.\n", name, attrval->name, attr->name);
            if (outFile != NULL)
               fprintf(outFile, "Object %s is already defined to not have %s as the value for attribute %s.\n", name, attrval->name, attr->name);
         }
         if (best != NULL) free(best);
         return;
      }
      if (isQuery) {
         correctScan = readInObjectAttribute(NULL, obj, name, attr->name, attrval->name);
         if (correctScan == 0) {
            if (outFile != NULL) fclose(outFile);
            if (best != NULL) free(best);
            return;
         }
         newLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, 1);
         removeAttributeToKB(obj, attr->name, attrval->name, pol);
         printf("P[%s(%s,%s)] = %f\n", attr->name, name, attrval->name, exp(newLogZ - logZ));
         if (outFile != NULL)
            fprintf(outFile, "P[%s(%s,%s)] = %f\n", attr->name, name, attrval->name, exp(newLogZ - logZ));
         if (best != NULL) free(best);
         return;
      } else {
         correctScan = readInObjectAttribute(kb, obj, name, attr->name, attrval->name);
         if (correctScan == 0) {
            if (outFile != NULL) fclose(outFile);
            if (best != NULL) free(best);
            return;
         }
         propagateKBChange(obj);
      }
   }
   if (best != NULL) free(best);
}

void computeRelationQueryOrAddEvidence(TMLKB* kb, char* relName, char* rest, int pol, float logZ, int isQuery, FILE* outFile) {
   char name_fmt_str[25];
   char base[MAX_NAME_LENGTH+1];
   char* iter = rest;
   char* normalizedRelStr;
   char* outputRelStr;
   int n, p;
   Name_and_Ptr* name_and_ptr;
   Name_and_Ptr* naptmp;
   char** args;
   QNode* list;
   TMLClass* cl;
   Node* tmp;
   Node* node;
   QNode* qnode;
  
   snprintf(name_fmt_str, 25, " %%%d[][a-zA-Z0-9.:] ", MAX_NAME_LENGTH);
   sscanf(iter, name_fmt_str, base);
   iter = strchr(iter, ',');
   if (iter == NULL) { // no args
      normalizedRelStr = splitRelArgsAndCreateNormalizedRelStr(kb, NULL, relName, &p, &args, 0);
      outputRelStr = splitRelArgsAndCreateNormalizedRelStr(kb, NULL, relName, &p, &args, 1);
   } else {
      iter++;
      normalizedRelStr = splitRelArgsAndCreateNormalizedRelStr(kb, iter, relName, &p, &args, 0);
      outputRelStr = splitRelArgsAndCreateNormalizedRelStr(kb, iter, relName, &p, &args, 1);
   }
   if (normalizedRelStr == NULL) {
      printf("Malformed relation %s(%s).\n", relName, rest);
      for (n = 0; n < p; n++)
         free(args[n]);
      free(args);
      return;
   }
   HASH_FIND_STR(kb->objectNameToPtr, base, node);
   if (node == NULL)
      node = findNodeFromAnonName(kb, NULL, base, 0);
   if (node == NULL) {
      if (isQuery == 0) {
         printf("Can only add facts about specific objects.\n");
         for (n = 0; n < p; n++)
            free(args[n]);
         free(args);
         free(normalizedRelStr);
         return;
      }
      HASH_FIND_STR(kb->classNameToPtr, base, cl);
      if (cl == NULL) {
         printf("Unknown object/class %s.\n", base);
         for (n = 0; n < p; n++)
            free(args[n]);
         free(args);
         free(normalizedRelStr);
         return;
      }
      list = kb->classToObjPtrs[cl->id];

      qnode = list;
      while (qnode != NULL) {
         tmp = (Node*)(qnode->ptr);
         if (tmp->name != NULL) {
            HASH_FIND_STR(kb->objectNameToPtr, tmp->name, node);
            computeRelationQueryOrAddEvidenceForObj(kb, node, tmp->name, relName, iter, pol, normalizedRelStr, args, p, logZ, isQuery, 1, outputRelStr, outFile);
         } else {
            HASH_FIND(hh_path, kb->objectPathToPtr, tmp->pathname, strlen(tmp->pathname), node);
            computeRelationQueryOrAddEvidenceForObj(kb, node, tmp->pathname, relName, iter, pol, normalizedRelStr, args, p, logZ, isQuery, 1, outputRelStr, outFile);
         }
         qnode = qnode->next;
      }
      for (n = 0; n < p; n++)
         free(args[n]);
      free(args);
      free(normalizedRelStr);
      return;

   }
   computeRelationQueryOrAddEvidenceForObj(kb, node, base, relName, iter, pol, normalizedRelStr, args, p, logZ, isQuery, 0, outputRelStr, outFile);
      for (n = 0; n < p; n++)
         free(args[n]);
      free(args);
      free(normalizedRelStr);
      return;
}

void traverseSPNToFindParts(Node* node, TMLClass* cl, Name_and_Ptr** partHashPtr) {
   TMLPart* part;
   TMLPart* tmp;
   int partIdx = 0;
   int p, c;
   Node* partNode;
   Name_and_Ptr* name_and_ptr;
   TMLClass* partcl;

   HASH_ITER(hh, node->cl->part, part, tmp) {
      if (part->defaultPart == 1) { partIdx++; continue; }
         partcl = part->cl;
         if (partcl->level >= cl->level) {
            while (partcl->level > cl->level) partcl = partcl->par;
            if (partcl == cl) {
               for (p = 0; p < part->n; p++) {
                  partNode = node->part[partIdx][p];
                  HASH_FIND_STR(*partHashPtr, partNode->name, name_and_ptr);
                  if (name_and_ptr == NULL) {
                     name_and_ptr = (Name_and_Ptr*)malloc(sizeof(Name_and_Ptr));
                     name_and_ptr->name = partNode->name;
                     name_and_ptr->ptr = partNode;
                     HASH_ADD_KEYPTR(hh, *partHashPtr, name_and_ptr->name, strlen(name_and_ptr->name), name_and_ptr);
                  }
               }
            } else {
               for (p = 0; p < part->n; p++)
                  traverseSPNToFindParts(node->part[partIdx][p], cl, partHashPtr);
            }
         } else { // Since we don't allow cycles of classes in part structure, we only traverse through parts of not the class in question
            for (p = 0; p < part->n; p++)
               traverseSPNToFindParts(node->part[partIdx][p], cl, partHashPtr);
         }
      partIdx++;
   }
   if (node->cl->nsubcls != 0) {
      if (node->assignedSubcl != -1) {
         if (node->subclMask == NULL) 
            traverseSPNToFindParts(node->subcl, cl, partHashPtr);
         else
            traverseSPNToFindParts(&(node->subcl[node->assignedSubcl]), cl, partHashPtr);
      } else {
         for (p = 0; p < node->cl->nsubcls; p++) {
            if (node->subclMask != NULL && node->subclMask[p] != 1) continue;
            traverseSPNToFindParts(&(node->subcl[p]), cl, partHashPtr);
         }
      }
   }
}

void testTraverseForClass(TMLKB* kb, const char* className) {
   Name_and_Ptr* name_and_ptr;
   Name_and_Ptr* traversal = NULL;
   Name_and_Ptr* tmp;
   TMLClass* cl;
   HASH_FIND_STR(kb->classNameToPtr, className, cl);
   if (cl == NULL) return;
   traverseSPNToFindParts((Node*)(kb->root->ptr), cl, &traversal);
   HASH_ITER(hh, traversal, name_and_ptr, tmp) {
      printf("%s\n", name_and_ptr->name);
   }
}

int computeNumRelationGroundings(Node* node, const char* relStr) {
   TMLRelation* rel = getRelation(node->cl, relStr);
   int numPoss = 1;
   TMLPart* part;

   if (rel == NULL) return 0;
}

void computeRelationQueryOrAddEvidenceForObj(TMLKB* kb, Node* topNode, char* base, char* relName, char* iter, int pol, char* normalizedRelStr, char** args, int p, float logZ, int isQuery, int isClassQuery, char* outputRelStr, FILE* outFile) {
   int i, j, k, n;
   Node*** argNodes;
   int* argLen;
   Node** currArgNodes;
   Node*** argParNodes;
   Node** par = NULL;
   int abstractQuery = 0;
   Name_and_Ptr* name_and_ptr;
   Name_and_Ptr* naptmp;
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash;
   Node* node;
   TMLRelation* rel;
   Node* foundrelNode = NULL;
   TMLRelation* foundrel;
   TMLRelation* tmp;
   int relIdx;
   char* partname;
   char* normalizedGroundStr;
   char* outputGroundStr;
   Node* subpartNode;
   Node* tmpNode;
   int partCl;
   char* partName;
   TMLPart* part;
   TMLPart* foundpart;
   TMLPart* tmppart;
   int partArrIdx;
   int partIdx;
   char* partBase;
   ArraysAccessor* aa;
   float blockedLogZ;
   float newLogZ;
   int combo;
   int recomputeLogZ;
   KBEdit* queryEdits = NULL;
   TMLClass* partcl;
   Name_and_Ptr* partHash;
   int numTraverseParts;
   char* name;
   char* partPtr;
   char* best = NULL;

   if (topNode->name != NULL) {
      name = topNode->name;
   } else {
      best = createBestPathname(kb, topNode);
      name = best;
   }
   HASH_FIND_STR(kb->objToRelFactStrs, topNode->pathname, objRelHash);
   if (objRelHash != NULL) {
      HASH_FIND_STR(objRelHash->hash, normalizedRelStr, relStrHash);
      if (relStrHash != NULL) {
         if (iter != NULL) iter--;
         if (isClassQuery == 1) {if (best != NULL) free(best); return; }
         if (relStrHash->pol == pol && pol == 1) {
            printf("P[%s(%s%s)] = 1.0 That relation with that polarity is defined for object.\n", relName, name, (iter == NULL) ? "" : iter);
            if (outFile != NULL) fprintf(outFile, "P[%s(%s%s)] = 1.0\n", relName, name, (iter == NULL) ? "" : iter);
         } else if (relStrHash->pol == pol) {
            printf("P[%s(%s%s)] = 0.0 That relation with that polarity is defined for object.\n", relName, name, (iter == NULL) ? "" : iter);
            if (outFile != NULL) fprintf(outFile, "P[%s(%s%s)] = 0.0\n", relName, name, (iter == NULL) ? "" : iter);
         } else if (pol == 1) {
            printf("P[%s(%s%s)] = 0.0 That relation with the opposite polarity is defined for object.\n", relName, name, (iter == NULL) ? "" : iter);
            if (outFile != NULL) fprintf(outFile, "P[%s(%s%s)] = 0.0\n", relName, name, (iter == NULL) ? "" : iter);
         } else {
            printf("P[%s(%s%s)] = 1.0 That relation with the opposite polarity is defined for object.\n", relName, name, (iter == NULL) ? "" : iter);
            if (outFile != NULL) fprintf(outFile, "P[%s(%s%s)] = 1.0\n", relName, name, (iter == NULL) ? "" : iter);
         }
         if (best != NULL) free(best); 
         return;
      }
   }
   node = topNode;
   rel = getRelation(node->cl, relName);
   if (rel == NULL) foundrelNode = NULL;
   else foundrelNode = topNode;
   while (node->assignedSubcl != -1) {
      if (node->subclMask == NULL)
         node = node->subcl;
      else
         node = &(node->subcl[node->assignedSubcl]);
      foundrel = getRelation(node->cl, relName);
      if (foundrel != NULL) {
         rel = foundrel;
         foundrelNode = node;
      }
   }
   if (rel == NULL) {
      printf("Relation %s not defined for object %s.\n", relName, name);
      if (best != NULL) free(best); 
      return;
   }

   relIdx = 0;
   HASH_ITER(hh, foundrelNode->cl->rel, foundrel, tmp) {
      if (foundrel == rel) break;
      relIdx++;
   }
      argNodes = (Node***)malloc(sizeof(Node**)*p);
      argLen = (int*)malloc(sizeof(int)*p);
      argParNodes = (Node***)malloc(sizeof(Node**)*p);
      for (i = 0; i < p; i++) {
         partname = args[i];
         HASH_FIND_STR(kb->objectNameToPtr, partname, subpartNode);
         if (subpartNode == NULL)
            subpartNode = findNodeFromAnonName(kb, topNode, partname, 0);
         if (subpartNode == NULL) {
            if (strcmp(partname, rel->argPartName[i]) == 0) {
               if (isQuery == 0) {
                  printf("Can only add facts about specific objects.\n");
                  for (n = 0; n < i; n++) {
                     free(argNodes[n]);
                  }
                  free(argNodes);
                  free(argLen);
                  free(argParNodes);
                  if (best != NULL) free(best); 
                  return;
               }
               abstractQuery = 1;
                  tmpNode = node;
                  do {
                     HASH_FIND_STR(tmpNode->cl->part, partname, part);
                     if (part == NULL) {
                        if (tmpNode->cl->par != NULL)
                           tmpNode = *(tmpNode->par);
                        else
                           break;
                     } else break;
                  } while (TRUE);
                  partArrIdx = 0;
                  HASH_ITER(hh, tmpNode->cl->part, foundpart, tmppart) {
                     if (strcmp(foundpart->name, partname) == 0) break;
                     partArrIdx++;
                  }
                  argNodes[i] = (Node**)malloc(sizeof(Node*)*part->n);
                  argLen[i] = part->n;
                  for (j = 0; j < part->n; j++) {
                     argNodes[i][j] = tmpNode->part[partArrIdx][j];
                  }
            } else {
               partBase = findBasePartName(partname, &partIdx);
               if (strcmp(partBase, rel->argPartName[i]) != 0) {
                   
                  printf("Object %s is not a %s part of object %s.\n", partname,
                     rel->argPartName[i], name);
                  for (n = 0; n < i; n++) {
                     free(argNodes[n]);
                  }
                  free(argNodes);
                  free(argLen);
                  free(argParNodes);
                  if (best != NULL) free(best); 
                  return;
               } else {
                  tmpNode = node;
                  do {
                     HASH_FIND_STR(tmpNode->cl->part, partBase, part);
                     if (part == NULL) {
                        if (tmpNode->cl->par != NULL)
                           tmpNode = *(tmpNode->par);
                        else
                           break;
                     } else break;
                  } while (TRUE);
                  if (part->n < partIdx) {
                     printf("There are only %d parts of name %s for object %s.\n", part->n, partBase,
                        name);
                     for (n = 0; n < i; n++) {
                        free(argNodes[n]);
                     }
                     free(argNodes);
                     free(argLen);
                     free(argParNodes);
                     if (best != NULL) free(best); 
                     return;
                  }
                  partArrIdx = 0;
                  HASH_ITER(hh, tmpNode->cl->part, foundpart, tmppart) {
                     if (strcmp(foundpart->name, partBase) == 0) break;
                     partArrIdx++;
                  }

                  argNodes[i] = (Node**)malloc(sizeof(Node*));
                  argLen[i] = 1;
                  argNodes[i][0] = tmpNode->part[partArrIdx][partIdx-1];
                  if (argNodes[i][0] == NULL) {
                     printf("The %dth %s part of class %s is not defined for any subclass. Currently, subclasses are considered exhaustive.\n", partIdx, part->name, tmpNode->cl->name);
                     for (n = 0; n < i; n++) {
                        free(argNodes[n]);
                     }
                     free(argNodes);
                     free(argLen);
                     free(argParNodes);
                     if (best != NULL) free(best);
                     return;
                  }
               }
            }
         } else {
            tmpNode = node;
            partCl = rel->argClass[i];
            partName = rel->argPartName[i];
            if (partCl != -1) {
               while (tmpNode->cl->id != partCl) {
                  tmpNode = *(tmpNode->par);
               }
            } else {
               argNodes[i] = (Node**)malloc(sizeof(Node*));
               argLen[i] = 1;
               argNodes[i][0] = subpartNode;
               if (argNodes[i][0] == NULL) {
                  printf("The part %s of class %s is not defined for any subclass. Currently, subclasses are considered exhaustive.\n", part->name, tmpNode->cl->name);
                  for (n = 0; n < i; n++) {
                     free(argNodes[n]);
                  }
                  free(argNodes);
                  free(argLen);
                  free(argParNodes);
                  if (best != NULL) free(best);
                  return;
               }
               // TODO: block subclasses which don't have this part
            }
            partArrIdx = 0;
            HASH_ITER(hh, tmpNode->cl->part, foundpart, tmppart) {
               if (strcmp(foundpart->name, partName) == 0) break;
               partArrIdx++;
            }
            n = getPart(tmpNode->cl, partName)->n;
            for (j = 0; j < n; j++) {
               if (tmpNode->part[partArrIdx][j] == subpartNode) break;
            }
            if (j == n) {
               printf("Object %s is not a %s part of object %s.\n", partname,
                  partName, name);
               for (n = 0; n < i; n++) {
                  free(argNodes[n]);
               }
               free(argNodes);
               free(argLen);
               free(argParNodes);
               if (best != NULL) free(best);
               return;
            }
            argNodes[i] = (Node**)malloc(sizeof(Node*));
            argLen[i] = 1;
            argNodes[i][0] = subpartNode;
            if (argNodes[i][0] == NULL) {
               printf("The 1st %s part of class %s is not defined for any subclass. Currently, subclasses are considered exhaustive.\n", part->name, tmpNode->cl->name);
               for (n = 0; n < i; n++) {
                  free(argNodes[n]);
               }
               free(argNodes);
               free(argLen);
               free(argParNodes);
               if (best != NULL) free(best);
               return;
            }
         }
      }
      if (!isQuery) {
         if (topNode->npars != 0) {
         par = (Node**)malloc(sizeof(Node*)*topNode->npars);
            for (i = 0; i < topNode->npars; i++) {
               par[i] = blockClassesForPartQuery(&(kb->edits), topNode->par[i], topNode, NULL);
            }
         }
      }
      
      if (abstractQuery == 1) {
         if (topNode->npars != 0) {
            par = (Node**)malloc(sizeof(Node*)*topNode->npars);
            for (i = 0; i < topNode->npars; i++) {
               par[i] = blockClassesForPartQuery(&queryEdits, topNode->par[i], topNode, NULL);
            }
         }
         aa = createArraysAccessor((void***)argNodes, p, argLen);
         n = numCombinationsInArraysAccessor(aa);
         for (combo = 0; combo < n; combo++) {
            currArgNodes = (Node**)nextArraysAccessor(aa);
            if (currArgNodes == NULL) {
               resetKBEdits(kb, queryEdits);
               queryEdits = NULL;
               continue;
            }
            normalizedGroundStr = createNormalizedRelStr(kb, relName, name, currArgNodes, p, 0, 0);
            HASH_FIND_STR(kb->objToRelFactStrs, topNode->pathname, objRelHash);
            if (objRelHash != NULL) {
               HASH_FIND_STR(objRelHash->hash, normalizedGroundStr, relStrHash);
               if (relStrHash != NULL) {
                  resetKBEdits(kb, queryEdits);
                  queryEdits = NULL;
                  continue;
               }
            }
            outputGroundStr = createNormalizedRelStr(kb, relName, name, currArgNodes, p, 1, 1);
            recomputeLogZ = 0;
            for (i = 0; i < p; i++) {
               if (currArgNodes[i]->npars == 0)
                  argParNodes[i] = NULL;
               else {
                  recomputeLogZ = 1;
                  argParNodes[i] = (Node**)malloc(sizeof(Node*)*currArgNodes[i]->npars);
                  for (j = 0; j < currArgNodes[i]->npars; j++) {
                     argParNodes[i][j] = blockClassesForPartQuery(&queryEdits, currArgNodes[i]->par[j], currArgNodes[i], NULL);
                  }
               }
            }
            propagateKBChange(node);
            if (par == NULL && recomputeLogZ == 0) blockedLogZ = logZ;
            else blockedLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, 1);
            addRelationToKB(NULL, topNode, relName, pol);
            propagateKBChange(node);
            newLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, 1);
            if (!isnan(blockedLogZ) && !isinf(blockedLogZ)) {
               printf("P[%s] = %f\n", outputGroundStr, exp(newLogZ - blockedLogZ));
               if (outFile != NULL)
                  fprintf(outFile, "P[%s] = %f\n", outputGroundStr, exp(newLogZ - blockedLogZ));
            }
            free(normalizedGroundStr);
            free(outputGroundStr);
            removeRelationToKB(node, relName, pol);
            resetKBEdits(kb, queryEdits);
            queryEdits = NULL;
            propagateKBChange(node);
         }
      } else {
         if (topNode->npars != 0) {
            par = (Node**)malloc(sizeof(Node*)*topNode->npars);
            for (i = 0; i < topNode->npars; i++) {
               if (isQuery) 
                  par[i] = blockClassesForPartQuery(&queryEdits, topNode->par[i], topNode, NULL);
               else
                  par[i] = blockClassesForPartQuery(&(kb->edits), topNode->par[i], topNode, NULL);
            }
         }
         recomputeLogZ = 0;
         for (i = 0; i < p; i++) {
            if (argNodes[i][0]->npars == 0)
               argParNodes[i] = NULL;
            else {
               recomputeLogZ = 1;
               argParNodes[i] = (Node**)malloc(sizeof(Node*)*argNodes[i][0]->npars);
               for (j = 0; j < argNodes[i][0]->npars; j++) {
                  if (isQuery)
                     argParNodes[i][j] = blockClassesForPartQuery(&queryEdits, argNodes[i][0]->par[j], argNodes[i][0], NULL);
                  else
                     argParNodes[i][j] = blockClassesForPartQuery(&(kb->edits), argNodes[i][0]->par[j], argNodes[i][0], NULL);
               }
            }
         }
         propagateKBChange(node);
         if (isQuery) {
            if (par == NULL && recomputeLogZ == 0) blockedLogZ = logZ;
            else blockedLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, 1);
         }
         if (isQuery)
            addRelationToKB(NULL, topNode, relName, pol);
         else
            addRelationToKB(kb, topNode, relName, pol);
         propagateKBChange(node);
         if (isQuery) {
            newLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, 1);
            if (!isnan(blockedLogZ) && !isinf(blockedLogZ)) {
               if (iter == NULL) {
                  printf("P[%s(%s)] = %f\n", relName, name, exp(newLogZ - blockedLogZ));
                  if (outFile != NULL)
                     fprintf(outFile, "P[%s(%s)] = %f\n", relName, name, exp(newLogZ - blockedLogZ));
               } else {
                  iter--;
                  printf("P[%s(%s%s)] = %f\n", relName, name, iter, exp(newLogZ - blockedLogZ));
                  if (outFile != NULL)
                     fprintf(outFile, "P[%s(%s%s)] = %f\n", relName, name, iter, exp(newLogZ - blockedLogZ));
               }
            }
            removeRelationToKB(node, relName, 1);
            resetKBEdits(kb, queryEdits);
            propagateKBChange(node);
         } else {
            currArgNodes = NULL;
            if (p != 0) {
               aa = createArraysAccessor((void***)argNodes, p, argLen);
               currArgNodes = (Node**)nextArraysAccessor(aa);
            }
            normalizedGroundStr = createNormalizedRelStr(kb, relName, name, currArgNodes, p, 0, 0);
            if (objRelHash == NULL) {
               objRelHash = (ObjRelStrsHash*)malloc(sizeof(ObjRelStrsHash));
               objRelHash->obj = topNode->pathname;
               objRelHash->hash = NULL;
               HASH_ADD_KEYPTR(hh, kb->objToRelFactStrs, objRelHash->obj, strlen(objRelHash->obj), objRelHash);
            }
            relStrHash = (RelationStr_Hash*)malloc(sizeof(RelationStr_Hash));
            if (pol == 0) {
               relStrHash->pol = 0;
               relStrHash->str = normalizedGroundStr;
            } else {
               relStrHash->pol = 1;
               relStrHash->str = normalizedGroundStr;
            }
            HASH_ADD_KEYPTR(hh, objRelHash->hash, relStrHash->str, strlen(relStrHash->str), relStrHash);
            newLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, 1);
            if (isnan(newLogZ) || isinf(newLogZ)) {
               if (pol == 1)
                  printf("Adding %s(%s) causes a contradiction. The relation has not been added.\n", relName, name);
               else
                  printf("Adding !%s(%s) causes a contradiction. The relation has not been added.\n", relName, name);
               removeRelationToKB(node, relName, 1);
               HASH_DEL(objRelHash->hash, relStrHash);
            } else {
               kb->edits = addKBEdit(topNode, -1, relStrHash->str, -1, -1, pol, kb->edits);
            }
         }
      }
   
}


void propagateKBChangeUp(Node* node) {
   Node** par;
   int p;
   node->changed = 1;
   if (node->par != NULL) {
      par = node->par;
      for (p = 0; p < node->npars; p++) {
         propagateKBChange(*(par));
         par++;
      }
   }
}

void propagateKBChangeDown(Node* node) {
   int c;
   node->changed = 1;
   if (node->cl->nsubcls == 0) return;
   if (node->subclMask != NULL) {
      for (c = 0; c < node->cl->nsubcls; c++)
         propagateKBChangeDown(&(node->subcl[c]));
   } else if (node->assignedSubcl != -1) {
      propagateKBChangeDown(node->subcl);
   } else {
      for (c = 0; c < node->cl->nsubcls; c++)
         propagateKBChangeDown(&(node->subcl[c]));
   }
}

void propagateKBChange(Node* node) {
   propagateKBChangeUp(node);
   if (node->cl->par == NULL)
      propagateKBChangeDown(node);
}

void resetOneKBEdit(TMLKB* kb, KBEdit* edit) {
   Node* node;
   int c;
   int* subclMask;
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash;
   Node* nextNode;
   QNode* qnode;
   QNode* classToObjPtrsList;

   node = edit->node;
   if (edit->relStr != NULL) {
      if (edit->pol != -1) {
         HASH_FIND_STR(kb->objToRelFactStrs, edit->node->pathname, objRelHash);
         HASH_FIND_STR(objRelHash->hash, edit->relStr, relStrHash);
         HASH_DEL(objRelHash->hash, relStrHash);
         free(relStrHash->str);
         free(relStrHash);
      } else {
         HASH_FIND_STR(kb->objToRelFactStrs, edit->node->pathname, objRelHash);
         if (objRelHash != NULL) {
            HASH_DEL(kb->objToRelFactStrs, objRelHash);
            free(objRelHash->obj);
            objRelHash->obj = edit->relStr;
            HASH_ADD_KEYPTR(hh, kb->objToRelFactStrs, objRelHash->obj, strlen(objRelHash->obj), objRelHash);
         }
         HASH_FIND_STR(kb->objectNameToPtr, edit->node->name, nextNode);
         HASH_DEL(kb->objectNameToPtr, nextNode);
         free(nextNode->name);
         renameNode(kb, edit->node, strdup(edit->relStr));
         HASH_ADD_KEYPTR(hh, kb->objectNameToPtr, nextNode->name, strlen(nextNode->name), nextNode);
      }
   } else if (edit->relIdx == -1) {
      if (edit->pol == 1) {
         c = node->cl->subcl[node->assignedSubcl]->id;
         classToObjPtrsList = kb->classToObjPtrs[c];
         kb->classToObjPtrs[c] = classToObjPtrsList->next;
         free(classToObjPtrsList);
         node->assignedSubcl = -1;
      } else {
         subclMask = &(node->subclMask[edit->subclIdx]);
         c = node->cl->subcl[edit->subclIdx]->id;
         (*subclMask == -1) ? *subclMask = 1 : (*subclMask)++;
         classToObjPtrsList = kb->classToObjPtrs[c];
         qnode = (QNode*)malloc(sizeof(QNode));
         qnode->ptr = &(node->subcl[edit->subclIdx]);
         qnode->next = classToObjPtrsList;
         kb->classToObjPtrs[c] = qnode;
      }
   } else {
      if (edit->pol == 0) {
         node->relValues[edit->relIdx][0]--;
         node->relValues[edit->relIdx][2]++;
      } else {
         node->relValues[edit->relIdx][1]--;
         node->relValues[edit->relIdx][2]++;
      }
   }
}

void resetKBEdits(TMLKB* kb, KBEdit* edits) {
   Node* node;
   KBEdit* edit;
   KBEdit* deledit;
   int c;
   int* subclMask;
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash;
   Node* nextNode;
   QNode* qnode;
   QNode* classToObjPtrsList;

   edit = edits;
   while (edit != NULL) {
      node = edit->node;
      if (edit->relStr != NULL) {
         if (edit->pol != -1) {
            HASH_FIND_STR(kb->objToRelFactStrs, edit->node->pathname, objRelHash);
            HASH_FIND_STR(objRelHash->hash, edit->relStr, relStrHash);
            HASH_DEL(objRelHash->hash, relStrHash);
            free(relStrHash->str);
            free(relStrHash);
         } else {
            HASH_FIND_STR(kb->objectNameToPtr, edit->node->name, nextNode);
            HASH_DEL(kb->objectNameToPtr, nextNode);
            free(nextNode->name);
            renameNode(kb, edit->node, NULL);
         }
      } else if (edit->relIdx == -1) {
         if (edit->pol == 1) {
            c = node->cl->subcl[node->assignedSubcl]->id;
            classToObjPtrsList = kb->classToObjPtrs[c];
            kb->classToObjPtrs[c] = classToObjPtrsList->next;
            free(classToObjPtrsList);
            node->assignedSubcl = -1;
         } else {
            subclMask = &(node->subclMask[edit->subclIdx]);
            c = node->cl->subcl[edit->subclIdx]->id;
            (*subclMask == -1) ? *subclMask = 1 : (*subclMask)++;
            classToObjPtrsList = kb->classToObjPtrs[c];
            qnode = (QNode*)malloc(sizeof(QNode));
            qnode->ptr = &(node->subcl[edit->subclIdx]);
            qnode->next = classToObjPtrsList;
            kb->classToObjPtrs[c] = qnode;
         }
      } else {
         if (edit->subclIdx = 0) {
            if (edit->pol == 0) {
               node->relValues[edit->relIdx][0]--;
               node->relValues[edit->relIdx][2]++;
            } else {
               node->relValues[edit->relIdx][1]--;
               node->relValues[edit->relIdx][2]++;
            }
         } else {
            if (edit->pol == 0) {
               node->attrValues[edit->relIdx][edit->valIdx] = 0;
            } else {
               node->assignedAttr[edit->relIdx] = NULL;
            }
         }
      }
      deledit = edit;
      edit = edit->prev;
      free(deledit);
   }
   kb->mapSet = 0;
}

void resetKB(TMLKB* kb) { 
   resetKBEdits(kb, kb->edits);
   kb->edits = NULL;
   propagateKBChange((Node*)(kb->root->ptr));
}

Node* findNodeForClass(Node* node, TMLClass* cl, const char* objName, const char* clName, FILE* outFile) {
   int i, c;
   Node* parNode;

   if (cl->par == node->cl) {
      if (node->assignedSubcl != -1) {
         if (node->cl->subcl[node->assignedSubcl] == cl) {
            if (node->subclMask != NULL) {
               return &(node->subcl[node->assignedSubcl]);
            } else {
               return node->subcl;
            }
         } else {
            printf("P[Is(%s,%s)] = 0.0  Object defined to be of a contradictory class.\n", objName, clName);
            if (outFile != NULL) fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
            return NULL;
         }
      }
      for (c = 0; c < node->cl->nsubcls; c++) {
         if (node->cl->subcl[c] == cl) {
            if (node->subclMask != NULL && node->subclMask[c] != 1) {
               printf("P[Is(%s,%s)] = 0.0  Object defined to be not of class %s.\n", objName, clName, clName);
               if (outFile != NULL) fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
               return NULL;
            }
            node->assignedSubcl = c;
            if (node->subclMask == NULL && node->cl->nsubcls != 0) {
               node->subclMask = (int*)malloc(sizeof(int)*node->cl->nsubcls);
               for (i = 0; i < node->cl->nsubcls; i++) {
                  node->subclMask[i] = 1;
               }
            }
            return &(node->subcl[c]);
         }
      }
   }
   if (cl->par != NULL) {
      parNode = findNodeForClass(node, cl->par, objName, clName, outFile);
      if (parNode != NULL) {
         if (parNode->assignedSubcl != -1) {
            if (parNode->cl->subcl[parNode->assignedSubcl] == cl) {
               return parNode->subcl;
            }
            printf("P[Is(%s,%s)] = 0.0  Object defined to be of a contradictory class.\n", objName, clName);
            if (outFile != NULL) fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
            return NULL;
         }
         for (c = 0; c < parNode->cl->nsubcls; c++) {
            if (parNode->cl->subcl[c] == cl) {
               if (parNode->subclMask != NULL && parNode->subclMask[c] != 1) {
                  printf("P[Is(%s,%s)] = 0.0  Object defined to be not of class %s.\n", objName, clName, clName);
               if (outFile != NULL) fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
                  return NULL;
               }
               parNode->assignedSubcl = c;
               if (parNode->subclMask == NULL && parNode->cl->nsubcls != 0) {
                  parNode->subclMask = (int*)malloc(sizeof(int)*parNode->cl->nsubcls);
                  for (i = 0; i < parNode->cl->nsubcls; i++) {
                     parNode->subclMask[i] = 1;
                  }
               }
               return &(parNode->subcl[c]);
            }
         }
      } else return NULL;
   }
   printf("P[Is(%s,%s)] = 0.0  Object defined to be of a contradictory class.\n", objName, clName);
   if (outFile != NULL) fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
   return NULL;
}

void computeClassQueryForObject(TMLKB* kb, const char* objName, Node* obj, const char* clName, TMLClass* cl, float logZ, FILE* outFile) {
   Node* node;
   Node* prevFinest;
   int subcl;
   float newLogZ;
   KBEdit* edits = NULL;
   Node* par;

   prevFinest = obj;
   if (cl->level == obj->cl->level) {
      if (cl == obj->cl) {
         printf("P[Is(%s,%s)] = 1.0  Object defined to be of class of %s.\n", objName, clName, clName);
         if (outFile != NULL)
            fprintf(outFile, "P[Is(%s,%s)] = 1.0\n", objName, clName);
      } else {
         printf("P[Is(%s,%s)] = 0.0  Object defined to be of a contradictory class.\n", objName, clName);
         if (outFile != NULL)
            fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
      }
      return;
   }
   while (prevFinest->assignedSubcl != -1) {
      if (cl->level == prevFinest->cl->level) {
         if (cl->id == prevFinest->cl->id) {
            printf("P[Is(%s,%s)] = 1.0  Object defined to be of class of %s.\n", objName, clName, clName);
            if (outFile != NULL)
               fprintf(outFile, "P[Is(%s,%s)] = 1.0\n", objName, clName);
         } else {
            printf("P[Is(%s,%s)] = 0.0  Object defined to be of a contradictory class.\n", objName, clName);         
            if (outFile != NULL)
               fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
         }
         return;
      }
      if (prevFinest->subclMask != NULL)
         prevFinest = &(prevFinest->subcl[prevFinest->assignedSubcl]);
      else
         prevFinest = prevFinest->subcl;
   }
   if (cl->level == prevFinest->cl->level) {
      if (cl->id == prevFinest->cl->id) {
         printf("P[Is(%s,%s)] = 1.0  Object defined to be of class of %s.\n", objName, clName, clName);
         if (outFile != NULL)
            fprintf(outFile, "P[Is(%s,%s)] = 1.0\n", objName, clName);
      } else {
         printf("P[Is(%s,%s)] = 0.0  Object defined to be of a contradictory class.\n", objName, clName);         
         if (outFile != NULL)
            fprintf(outFile, "P[Is(%s,%s)] = 0.0\n", objName, clName);
      }
      return;
   }

   node = findNodeForClass(prevFinest, cl, objName, clName, outFile);
   if (node == NULL) return;
   par = obj;
   while (par->cl->par != NULL) par = *(par->par);
   if (par->npars != 0) {
      par = *(par->par);
      blockClassesForPartQuery(&edits, par, obj, cl);
   }

   propagateKBChange(node);
   newLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, (kb->mapSet == 1) ? 1: 0);
   printf("P[Is(%s,%s)] = %f\n", objName, clName, exp(newLogZ - logZ));
   if (outFile != NULL)
      fprintf(outFile, "P[Is(%s,%s)] = %f\n", objName, clName, exp(newLogZ - logZ));
   propagateKBChange(node);

   while (prevFinest->assignedSubcl != -1) {
      subcl = prevFinest->assignedSubcl;
      prevFinest->assignedSubcl = -1;
      prevFinest = &(prevFinest->subcl[subcl]);
   }
   resetKBEdits(kb, edits);
}

float computeQueryOrAddEvidence(TMLKB* kb, char* query, float logZ, int isQuery, const char* output) {
   FILE* outFile = NULL;
   char is_fmt_str[50];
   char has_fmt_str[60];
   char rel_fmt_str[30];
   char attr_fmt_str[30];
   char attrrel_fmt_str[50];
   char excl[2];
   int correctScan;
   int pol;
   char* iter;
   QNode* qnode;
   QNode* classToObjPtrsList;
   char* best;

   char clName[MAX_NAME_LENGTH+1];
   TMLClass* cl;
   char objName[MAX_NAME_LENGTH+1];
   Node* obj;
   Node* newnode;

   char subObjName[MAX_NAME_LENGTH+1];
   char subpartRelName[MAX_NAME_LENGTH+1];
   Node* subObj;
   int p;
   Node* par;
   char* bestParName;
   char* partName;
   int i, n;
   TMLPart* part;
   int maxParts;

   char relationName[MAX_NAME_LENGTH+1];
   char valName[MAX_NAME_LENGTH+1];
   TMLAttribute* attr;
   TMLAttrValue* attrval;
   float newLogZ;
   TMLClass* tmpcl;
   QNode* list;
   Node* tmp;
   KBEdit* tmpedits = NULL;
   KBEdit* edit;
   KBEdit* deledit;

   if (output != NULL) {
      outFile = fopen(output, "w");
      if (outFile == NULL) {
         printf("Error opening %s\n", output);
         return logZ;
      }
   }
   snprintf(is_fmt_str, 50, " Is ( %%%d[^, \t\r\n\v\f] , %%%d[^, \t\r\n\v\f] %%1s", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(has_fmt_str, 60, " Has ( %%%d[^, \t\r\n\v\f] , %%%d[^, \t\r\n\v\f] , %%%d[^, \t\r\n\v\f] %%1s", MAX_NAME_LENGTH, MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(rel_fmt_str, 30, "%%%d[^( \t\r\n\v\f] ", MAX_NAME_LENGTH);
   snprintf(attrrel_fmt_str, 50, "%%%d[^( \t\r\n\v\f] ( %%%d[^, \t\r\n\v\f] %%1s ", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
   snprintf(attr_fmt_str, 30, "%%%d[^ \t\r\n\v\f] %%1s", MAX_NAME_LENGTH);
   
   correctScan = sscanf(query, "%1s", excl);
   if (correctScan != 1) {
      if (outFile != NULL) {
         if (isQuery)
            fprintf(outFile, "Malformed query %s.\n", query);
         else
            fprintf(outFile, "Malformed fact %s.\n", query);
      }
      if (isQuery)
         printf("Malformed query %s.\n", query);
      else
         printf("Malformed fact %s.\n", query);
      if (outFile != NULL) fclose(outFile);
      return logZ;
   }

   if (excl[0] == '!') {
      pol = 0;
      iter = strchr(query, '!')+1;
   } else {
      pol = 1;
      iter = query;
   }
   correctScan = sscanf(iter, is_fmt_str, objName, clName, excl);
   if (correctScan == 2) { // subclass fact / query
      HASH_FIND_STR(kb->objectNameToPtr, objName, obj);
      if (obj == NULL)
         obj = findNodeFromAnonName(kb, NULL, objName, 0);
      if (obj == NULL && isQuery) {
         printf("Unknown object %s.\n", objName);
         if (outFile != NULL) {
            fprintf(outFile, "Unknown object %s.\n", objName);
            fclose(outFile);
         }
         return logZ;
      }
      if (obj != NULL && obj->pathname == NULL && isQuery) {
         printf("%s is not a descendant of the top object.\n", objName);
         if (outFile != NULL) {
            fprintf(outFile, "%s is not a descendant of the top object.\n", objName);
            fclose(outFile);
         }
         return logZ;
      }
      HASH_FIND_STR(kb->classNameToPtr, clName, cl);
      if (cl == NULL) {
         printf("Unknown class %s.\n", clName);
         if (outFile != NULL)
            fprintf(outFile, "Unknown class %s.\n", clName);
         if (outFile != NULL) fclose(outFile);
         return logZ;
      }
      if (isQuery) {
         if (obj->name != NULL)
            computeClassQueryForObject(kb, obj->name, obj,
               clName, cl, logZ, outFile);
         else {
            best = createBestPathname(kb, obj);
            computeClassQueryForObject(kb, best, obj,
               clName, cl, logZ, outFile);
            free(best);
         }
         if (outFile != NULL) fclose(outFile);
         return logZ;
      } else {
         if (obj == NULL) {
            printf("Unknown object %s\n", objName);
            return logZ;
         } else {
            if (pol == 1) {
               updateClassForNode(kb, &tmpedits, obj->name, obj,
               cl, NULL, -1);
            } else {
               newnode = blockClassForNode(kb, &tmpedits, obj->name, obj, cl, NULL, -1);
               if (newnode != NULL) {
                  classToObjPtrsList = kb->classToObjPtrs[cl->id];
                  qnode = classToObjPtrsList;
                  while (qnode != NULL && strcmp(((Node*)(qnode->ptr))->pathname, newnode->pathname) != 0) {
                     classToObjPtrsList = qnode;
                     qnode = qnode->next;
                  }
                  if (qnode != NULL) {
                     classToObjPtrsList->next = qnode->next;
                     free(qnode);
                  }
               }
            }
            par = obj;
            while (par->cl->par != NULL) par = *(par->par);
            if (par->npars != 0) {
               par = *(par->par);
               blockClassesForPartQuery(&tmpedits, par, obj, cl);
            }
         }
         propagateKBChange(obj);
         if (outFile != NULL) fclose(outFile);
         newLogZ = computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, (kb->mapSet == 1) ? 1: 0);
         if (isinf(newLogZ) || isnan(newLogZ)) {
            printf("That causes the knowledge base to be impossible. Nothing is done.\n");
            resetKBEdits(kb, tmpedits);
            return logZ;
         } else {
            edit = tmpedits;
            while (edit != NULL) {
              kb->edits = addAndCopyKBEdit(edit, kb->edits);
               deledit = edit;
               edit = edit->prev;
               free(deledit);
            }   
            return newLogZ;
         }
      }
      return logZ;
   }
   correctScan = sscanf(iter, has_fmt_str, objName, subObjName, subpartRelName, excl);
   if (correctScan == 3) { // subpart fact / query
      HASH_FIND_STR(kb->objectNameToPtr, objName, obj);
      if (obj == NULL)
         obj = findNodeFromAnonName(kb, NULL, objName, 0);
      if (obj == NULL) {
         printf("Unknown object %s.\n", objName);
         if (outFile != NULL)
            fprintf(outFile, "Unknown object %s.\n", objName);
         if (outFile != NULL) fclose(outFile);
         return logZ;
      }
      if (obj->pathname == NULL && isQuery) {
         printf("%s is not a descendant of the top object.\n", objName);
         if (outFile != NULL) {
            fprintf(outFile, "%s is not a descendant of the top object.\n", objName);
            fclose(outFile);
         }
         return logZ;
      }
      HASH_FIND_STR(kb->objectNameToPtr, subObjName, subObj);
      if (subObj != NULL) {
         if (subObj == kb->root->ptr) {
            if (isQuery) {
               printf("No.\n");
               if (outFile != NULL)
                  fprintf(outFile, "No.\n");
            } else {
               printf("The Top Object cannot be a subpart of another object.\n");
               if (outFile != NULL)
                  fprintf(outFile, "The Top Object cannot be a subpart of another object.\n");
            }
            if (outFile != NULL) fclose(outFile);
            return logZ;
         }
         if (subObj->npars == 0) {
            printf("No.\n");
            if (outFile != NULL) {
               fprintf(outFile, "No.\n");
               fclose(outFile);
            }
            return logZ;
         }
         for (p = 0; p < subObj->npars; p++) {
            par = subObj->par[p];
            if (strcmp(par->pathname, obj->pathname) == 0) break;
         }
         if (p == subObj->npars) {
            bestParName = createBestPathname(kb, par);
            if (isQuery) {
               printf("No. %s is a subpart of %s.\n", subObjName, bestParName);
               if (outFile != NULL) {
                  fprintf(outFile, "No. %s is a subpart of %s.\n", subObjName, bestParName);
                  fclose(outFile);
               }
               free(bestParName);
               return logZ;
            } else {
               printf("%s is already a subpart of %s.\n", subObjName, bestParName);
               if (outFile != NULL) {
                  fprintf(outFile, "%s is already a subpart of %s.\n", subObjName, bestParName);
                  fclose(outFile);
               }
               free(bestParName);
               return logZ;
               // TODO Add mult parents
            }
         }

         partName = findBasePartName(subpartRelName, &n);
         part = getPart(par->cl, partName);
         if (part == NULL) {
            if (isQuery) {
               printf("No. %s is not subpart of %s with relation %s.\n", subObjName, objName, partName);
               if (outFile != NULL)
                  fprintf(outFile, "No. %s is not subpart of %s with relation %s.\n", subObjName, objName, partName);
            } else {
               printf("%s is already a subpart of %s with a different relation.\n", subObjName, objName);
               if (outFile != NULL)
                  fprintf(outFile, "%s is already a subpart of %s with a different relation.\n", subObjName, objName);
            }
            free(partName);
            if (outFile != NULL) fclose(outFile);
            return logZ;
         }
         for (i = 0; i < part->n; i++) {
            if (par->part[part->idx][i] == subObj) {
               if (i == (n-1)) {
                  if (isQuery) {
                     printf("Yes.\n");
                     if (outFile != NULL)
                        fprintf(outFile, "Yes.\n");
                  } else {
                     printf("%s is already the %d %s part of %s.\n", subObjName, n, partName, objName);
                     if (outFile != NULL)
                        fprintf(outFile, "%s is already the %d %s part of %s.\n", subObjName, n, partName, objName);
                  }
               } else {
                  if (isQuery) {
                     printf("No. %s is the %d %s part of %s.\n", subObjName, (i+1), partName, objName);
                     if (outFile != NULL)
                        fprintf(outFile, "No. %s is the %d %s part of %s.\n", subObjName, (i+1), partName, objName);
                  } else {
                     printf("%s is already the %d %s part of %s.\n", subObjName, (i+1), partName, objName);
                     if (outFile != NULL)
                        fprintf(outFile, "%s is already the %d %s part of %s.\n", subObjName, (i+1), partName, objName);
                  }
               }
               free(partName);
               if (outFile != NULL) fclose(outFile);
               return logZ;
            }
         }
         if (isQuery) {
            printf("No. %s is not subpart of %s with relation %s.\n", subObjName, objName, partName);
            if (outFile != NULL)
               fprintf(outFile, "No. %s is not subpart of %s with relation %s.\n", subObjName, objName, partName);
         } else {
            printf("%s is already a subpart of %s with a different relation.\n", subObjName, objName, partName);
            if (outFile != NULL)
               fprintf(outFile, "%s is already a subpart of %s with a different relation.\n", subObjName, objName, partName);
         }
         free(partName);
         if (outFile != NULL) fclose(outFile);
         return logZ;
      } else {
         if (isQuery) {
            printf("Unknown object %s.\n", subObjName);
            if (outFile != NULL) fclose(outFile);
            return logZ;
         }
         if (strpbrk(subObjName, "0123456789") == subObjName) {
            printf("Object names must begin with a letter. %s does not.\n", subObjName);
            if (outFile != NULL) fclose(outFile);
            return logZ;
         }
         partName = findBasePartName(subpartRelName, &n);
         subObj = findPartDown(obj, partName, n, 1, &maxParts);
         if (outFile != NULL) fclose(outFile);
         if (subObj == NULL) {
            return logZ;
         }
         if (subObj->name != NULL) {
            printf("%s already has %s as its %d %s part.\n", objName, subObj->name, n, partName);
            if (outFile != NULL) fclose(outFile);
            free(partName);
            return logZ;
         }
         free(partName);
         partName = strdup(subObjName);
         kb->edits = addKBEdit(subObj, -1, subObj->pathname, -1, -1, -1, kb->edits);
         HASH_FIND(hh_path, kb->objectPathToPtr, subObj->pathname, strlen(subObj->pathname), newnode);
         renameNode(kb, newnode, partName);
         HASH_ADD_KEYPTR(hh, kb->objectNameToPtr, newnode->name, strlen(newnode->name), newnode);
         blockClassesForPartQuery(&(kb->edits), *(newnode->par), newnode, NULL);
         propagateKBChange(obj);
         return computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, (kb->mapSet == 1) ? 1: 0);
      }
   }
   correctScan = sscanf(iter, attrrel_fmt_str, relationName, objName, excl);
   if (correctScan < 2 || (correctScan == 3 && excl[0] != ',')) {
      if (outFile != NULL) {
         if (isQuery)
            fprintf(outFile, "Malformed query %s.\n", query);
         else
            fprintf(outFile, "Malformed fact %s.\n", query);
      }
      if (isQuery)
         printf("Malformed query %s.\n", query);
      else
         printf("Malformed fact %s.\n", query);
      if (outFile != NULL) fclose(outFile);
      return logZ;
   }
   HASH_FIND_STR(kb->objectNameToPtr, objName, obj);
   if (obj == NULL)
      obj = findNodeFromAnonName(kb, NULL, objName, 0);
   if (obj == NULL)
      HASH_FIND_STR(kb->classNameToPtr, objName, cl);
   if (obj != NULL || cl == NULL) {
      if (obj == NULL) {
         if (isQuery) {
            printf("%s is not a descendant of the top object.\n", objName);
            if (outFile != NULL) {
               fprintf(outFile, "%s is not a descendant of the top object.\n", objName);
               fclose(outFile);
            }
            return logZ;
         } else {
            if (strpbrk(objName, "0123456789") == objName) {
               printf("Object names must begin with a letter. %s does not.\n", objName);
               if (outFile != NULL) fclose(outFile);
               return logZ;
            }
            obj = initNodeToClass(kb, strdup(objName), cl, cl);
            obj->pathname = NULL;
            printf("Warning: %s is unknown and therefore is not a descendant of the top object.\n", objName);
         }
      }
      if (obj->pathname == NULL && isQuery) {
         printf("%s is not a descendant of the top object.\n", objName);
         if (outFile != NULL) {
            fprintf(outFile, "%s is not a descendant of the top object.\n", objName);
            fclose(outFile);
         }
         return logZ;
      }
      cl = obj->cl;
      newnode = obj;
      while (TRUE) {
         attr = getAttribute(cl, relationName);
         if (attr != NULL) break;
         if (cl->nsubcls == 0 || newnode->assignedSubcl == -1) break;
         if (newnode->subclMask == NULL) newnode = newnode->subcl;
         else newnode = &(newnode->subcl[newnode->assignedSubcl]);
         cl = newnode->cl;
      }
      if (attr != NULL) {
         if (correctScan == 2) { // check all values
            computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, NULL, pol, logZ, isQuery, 1, outFile);
            return logZ;
         } else {
            iter = strchr(iter, ',')+1;
            correctScan = sscanf(iter, attr_fmt_str, valName, excl);
            if (correctScan != 1) {
               if (outFile != NULL) {
                  if (isQuery)
                     fprintf(outFile, "Malformed query %s.\n", query);
                  else
                     fprintf(outFile, "Malformed fact %s.\n", query);
               }
               if (isQuery)
                  printf("Malformed query %s.\n", query);
               else
                  printf("Malformed fact %s.\n", query);
               if (outFile != NULL) fclose(outFile);
               return logZ;
            }
            HASH_FIND_STR(attr->vals, valName, attrval);
            if (attrval == NULL) {
               printf("%s is not a valid value for attribute %s in objects of class %s.\n", valName, relationName, objName);
               return logZ;
            }
            computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, attrval, pol, logZ, isQuery, 0, outFile);
            if (isQuery) return logZ;
            else return computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, (kb->mapSet == 1) ? 1: 0);
         }
      }
   } else {  //check if it is a class
      if (isQuery == 0) {
         printf("Can only add facts about specific objects.\n");
         return logZ;
      }
      tmpcl = cl;
      while (tmpcl != NULL) {
         attr = getAttribute(tmpcl, relationName);
         if (attr != NULL) break;
         tmpcl = tmpcl->par;
      }
      if (attr != NULL) {
         if (correctScan == 2) { // check all values
            list = kb->classToObjPtrs[cl->id];

            qnode = list;
            while (qnode != NULL) {
               tmp = (Node*)(qnode->ptr);
               if (tmp->name != NULL) {
                  HASH_FIND_STR(kb->objectNameToPtr, tmp->name, obj);
                  computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, NULL, pol, logZ, isQuery, 1, outFile);
               } else {
                  HASH_FIND(hh_path, kb->objectPathToPtr, tmp->pathname, strlen(tmp->pathname), obj);
                  computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, NULL, pol, logZ, isQuery, 1, outFile);
               }
               qnode = qnode->next;
            }
            return logZ;
         } else {
            iter = strchr(iter, ',')+1;
            correctScan = sscanf(iter, attr_fmt_str, valName, excl);
            if (correctScan != 1) {
               if (outFile != NULL) {
                  if (isQuery)
                     fprintf(outFile, "Malformed query %s.\n", query);
                  else
                     fprintf(outFile, "Malformed fact %s.\n", query);
               }
               if (isQuery)
                  printf("Malformed query %s.\n", query);
               else
                  printf("Malformed fact %s.\n", query);
               if (outFile != NULL) fclose(outFile);
               return logZ;
            }
            HASH_FIND_STR(attr->vals, valName, attrval);
            if (attrval == NULL) {
               printf("%s is not a valid value for attribute %s in objects of class %s.\n", valName, relationName, objName);
               return logZ;
            }
            list = kb->classToObjPtrs[cl->id];

            qnode = list;
            while (qnode != NULL) {
               tmp = (Node*)(qnode->ptr);
               if (tmp->name != NULL) {
                  HASH_FIND_STR(kb->objectNameToPtr, tmp->name, obj);
                  computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, attrval, pol, logZ, isQuery, 1, outFile);
               } else {
                  HASH_FIND(hh_path, kb->objectPathToPtr, tmp->pathname, strlen(tmp->pathname), obj);
                  computeAttributeQueryOrAddEvidenceForObj(kb, obj, attr, attrval, pol, logZ, isQuery, 1, outFile);
               }
               qnode = qnode->next;
            }
         }
         return logZ;
      }
   }
   iter = strchr(query, '(')+1;
   if (isQuery) {
      computeRelationQueryOrAddEvidence(kb, relationName, iter, pol, logZ, 1, outFile);
      if (outFile != NULL) fclose(outFile);
      return logZ;
   } else {
      computeRelationQueryOrAddEvidence(kb, relationName, iter, pol, logZ, 0, outFile);
      if (outFile != NULL) fclose(outFile);
      return computeLogZ((Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, spn_logsum, (kb->mapSet == 1) ? 1: 0);
   }
}

ArraysAccessor* createArraysAccessorForRel(TMLRelation* rel, Node* node) {
   int a, p, i;
   Node*** args = (Node***)malloc(sizeof(Node**)*rel->nargs);
   int* argLen = (int*)malloc(sizeof(int)*rel->nargs);
   Node* tmpNode;
   int* argClass = rel->argClass;
   char** argPartName = rel->argPartName;
   int clId;
   char* partName;
   TMLPart* part;
   TMLPart* tmpPart;

   for (a = 0; a < rel->nargs; a++) {
      clId = *argClass;
      partName = *argPartName;
      tmpNode = node;
      while (tmpNode->cl->id != clId) {
         tmpNode = *(tmpNode->par);
      }
      p = 0;
      HASH_ITER(hh, tmpNode->cl->part, part, tmpPart) {
         if (strcmp(part->name, partName) == 0) break;
         p++;
      }
      argLen[a] = part->n;
      args[a] = (Node**)malloc(sizeof(Node*)*part->n);
      for (i = 0; i < part->n; i++) {
         args[a][i] = tmpNode->part[p][i];
      }

      argClass++;
      argPartName++;
   }
   return createArraysAccessor((void***)args, rel->nargs, argLen);
}

void printMAPStateRec(TMLKB* kb, Node* node, TMLClass* assignedClFromSubpart, FILE* outFile) {
   int r = 0;
   TMLRelation* rel;
   TMLRelation* temprel;
   TMLAttribute* attr;
   TMLAttribute* tempattr;
   TMLAttrValue* attrval;
   TMLAttrValue* tempval;
   TMLPart* part;
   TMLPart* tmp;
   int nextSubcl = node->assignedSubcl;
   ArraysAccessor* aa;
   int ncombo;
   int c;
   Node** currArgNodes;
   char* normalizedGroundStr;
   char* outputGroundStr;
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash;
   char* name;
   char* partPtr;
   int p, i;
   char* best = NULL;
   float max;
   TMLAttrValue* maxVal;
   int descendantIdx = isDescendant(assignedClFromSubpart, node->cl);

   if (nextSubcl == -1 && node->cl->nsubcls != 0)
      nextSubcl = node->maxSubcl;

   if (node->name != NULL)
      name = node->name;
   else {
      best = createBestPathname(kb, node);
      name = best;
   }
   HASH_FIND_STR(kb->objToRelFactStrs, name, objRelHash);
   HASH_ITER(hh, node->cl->rel, rel, temprel) {
      if (rel->defaultRel == 0 || rel->defaultRelForSubcl[nextSubcl] == 0) {
         if (node->relValues[r][2] != 0 || rel->hard != 0) {
            if (rel->nargs != 0) {
               aa = createArraysAccessorForRel(rel, node);
               ncombo = numCombinationsInArraysAccessor(aa);
               for (c = 0; c < ncombo; c++) {
                  currArgNodes = (Node**)nextArraysAccessor(aa);
                  normalizedGroundStr = createNormalizedRelStr(kb, rel->name, node->pathname, currArgNodes, rel->nargs, 0, 0);
                  outputGroundStr = createNormalizedRelStr(kb, rel->name, name, currArgNodes, rel->nargs, 1, 1);
                  if (objRelHash != NULL && rel->hard == 0) {
                     HASH_FIND_STR(objRelHash->hash, normalizedGroundStr, relStrHash);
                     if (relStrHash != NULL) continue;
                  }
                  if (outFile == NULL)
                     printf("%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
                  else
                     fprintf(outFile, "%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
               }
            } else {
               normalizedGroundStr = createNormalizedRelStr(kb, rel->name, node->pathname, NULL, 0, 0, 0);
               outputGroundStr = createNormalizedRelStr(kb, rel->name, name, NULL, 0, 1, 1);
               if (objRelHash != NULL && rel->hard == 0) {
                  HASH_FIND_STR(objRelHash->hash, normalizedGroundStr, relStrHash);
                  if (relStrHash != NULL) continue;
               }
               if (outFile == NULL)
                  printf("%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
               else
                  fprintf(outFile, "%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
            }
         }
      }
      r++;
   }
   HASH_ITER(hh, node->cl->attr, attr, tempattr) {
      if (attr->defaultAttr == 0 || attr->defaultAttrForSubcl[nextSubcl] == 0) {
         if (node->assignedAttr[attr->idx] == NULL) {
            if (node->attrValues[attr->idx] == NULL) {
               max = attr->vals->wt;
               maxVal = attr->vals;
               HASH_ITER(hh, attr->vals, attrval, tempval) {
                 if (attrval->wt > max) {
                     max = attrval->wt;
                     maxVal = attrval;
                  } 
               }
            } else {
               HASH_ITER(hh, attr->vals, attrval, tempval) {
                  if (node->attrValues[attr->idx][attrval->idx] >= 0) {
                     max = attrval->wt;
                     maxVal = attr->vals;
                     break;
                  }
               }
               HASH_ITER(hh, attr->vals, attrval, tempval) {
                  if (node->attrValues[attr->idx][attrval->idx] < 0) continue;
                  if (attrval->wt > max) {
                     max = attrval->wt;
                     maxVal = attrval;
                  } 
               }
            }
            if (outFile == NULL)
               printf("%s(%s,%s)\n", attr->name, name, maxVal->name);
            else
               fprintf(outFile, "%s(%s,%s)\n", attr->name, name, maxVal->name);
         }
      }
   }
   p = 0; 
   HASH_ITER(hh, node->cl->part, part, tmp) {
      if (part->defaultPart == 0 || part->defaultPartForSubcl[nextSubcl] == 0) {
         for (i = 0; i < part->n; i++) {
            printMAPStateRec(kb, node->part[p][i], part->cl, outFile);
         }
      }
      p++;
   }

   if (node->cl->nsubcls != 0) {
      if (node->assignedSubcl != -1) {
         if (node->subclMask == NULL)
            printMAPStateRec(kb, node->subcl, assignedClFromSubpart, outFile);
         else
            printMAPStateRec(kb, &(node->subcl[node->assignedSubcl]), assignedClFromSubpart, outFile);
      } else if (descendantIdx != -1) {
         if (outFile == NULL)
            printf("Is(%s,%s)\n", name, node->cl->subcl[descendantIdx]->name);
         else
            fprintf(outFile, "Is(%s,%s)\n", name, node->cl->subcl[descendantIdx]->name);
         printMAPStateRec(kb, &(node->subcl[descendantIdx]), assignedClFromSubpart, outFile);
      } else {
         if (outFile == NULL)
            printf("Is(%s,%s)\n", name, node->cl->subcl[node->maxSubcl]->name);
         else
            fprintf(outFile, "Is(%s,%s)\n", name, node->cl->subcl[node->maxSubcl]->name);
         printMAPStateRec(kb, &(node->subcl[node->maxSubcl]), assignedClFromSubpart, outFile);
      }
   }
   if (best != NULL) free(best);
}

void printMAPStateForObj(TMLKB* kb, Node* node, FILE* outFile) {
   int r = 0;
   TMLRelation* rel;
   TMLRelation* temprel;
   int nextSubcl = node->assignedSubcl;
   ArraysAccessor* aa;
   int ncombo;
   int c;
   Node** currArgNodes;
   char* normalizedGroundStr;
   char* outputGroundStr;
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash;
   char* name;
   char* partPtr;

   if (nextSubcl == -1 && node->cl->nsubcls != 0)
      nextSubcl = node->maxSubcl;
   if (node->name != NULL)
      name = node->name;
   else
      name = node->pathname;
   HASH_FIND_STR(kb->objToRelFactStrs, name, objRelHash);
   HASH_ITER(hh, node->cl->rel, rel, temprel) {
      if (rel->defaultRel == 0 || rel->defaultRelForSubcl[nextSubcl] == 0) {
         if (node->relValues[r][2] != 0) {
            if (rel->nargs != 0) {
               aa = createArraysAccessorForRel(rel, node);
               ncombo = numCombinationsInArraysAccessor(aa);
               for (c = 0; c < ncombo; c++) {
                  currArgNodes = (Node**)nextArraysAccessor(aa);
                  normalizedGroundStr = createNormalizedRelStr(kb, rel->name, name, currArgNodes, rel->nargs, 0, 0);
                  outputGroundStr = createNormalizedRelStr(kb, rel->name, node->name, currArgNodes, rel->nargs, 1, 1);
                  if (objRelHash != NULL) {
                     HASH_FIND_STR(objRelHash->hash, normalizedGroundStr, relStrHash);
                     if (relStrHash != NULL) continue;
                  }
                  if (outFile == NULL)
                     printf("%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
                  else
                     fprintf(outFile, "%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
               }
            } else {
               normalizedGroundStr = createNormalizedRelStr(kb, rel->name, node->name, NULL, 0, 0, 0);
               outputGroundStr = createNormalizedRelStr(kb, rel->name, node->name, NULL, 0, 1, 1);
               if (objRelHash != NULL) {
                  HASH_FIND_STR(objRelHash->hash, normalizedGroundStr, relStrHash);
                  if (relStrHash != NULL) continue;
               }
               if (outFile == NULL)
                  printf("%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
               else
                  fprintf(outFile, "%s%s\n", ((rel->pwt > rel->nwt) ? "" : "!"), outputGroundStr);
            }
         }
      }
      r++;
   }   

   if (node->cl->nsubcls != 0) {
      if (node->assignedSubcl != -1) {
         if (node->subclMask == NULL)
            printMAPStateForObj(kb, node->subcl, outFile);
         else
            printMAPStateForObj(kb, &(node->subcl[node->assignedSubcl]), outFile);
      } else {
         if (outFile == NULL)
            printf("Is(%s,%s)\n", node->name, node->cl->subcl[node->maxSubcl]->name);
         else
            fprintf(outFile, "Is(%s,%s)\n", node->name, node->cl->subcl[node->maxSubcl]->name);
         printMAPStateForObj(kb, &(node->subcl[node->maxSubcl]), outFile);
      }
   }
}

void printMAPState(TMLKB* kb, const char* outFileName) {
   FILE* outFile = NULL;
   Node* node;
   Node* tmp;

   if (outFileName == NULL)
      printf("MAP state of unknown TML facts:\n");
   else
      outFile = fopen(outFileName, "w");

   printMAPStateRec(kb, (Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, outFile);
}

void computeMAPState(TMLKB* kb, float logZ) {
   float mapStateLogZ;
   Node* root = (Node*)(kb->root->ptr);
   if (kb->mapSet != 1) {
      computeLogZ(root, root->cl, spn_max, 1);
      kb->mapSet = 1;
   } else
      computeLogZ(root, root->cl, spn_max, 0);
   
}

/* Cleaning up the TMLKB structure */
void freeTMLKB(void* obj) {
   TMLKB* kb = (TMLKB*)obj;
   Node* node;
   Node* tmp;
   QNode* qnode;
   QNode* next;
   ObjRelStrsHash* objRelHash;
   ObjRelStrsHash* objRelTemp;
   RelationStr_Hash* relStrHash;
   RelationStr_Hash* relStrTemp;
   TMLClass* cl;
   TMLClass* tmpcl;
   int c;

   if (kb->root != NULL) {
      free(kb->root);
   }

   HASH_ITER(hh, kb->objectNameToPtr, node, tmp) {
      HASH_DEL(kb->objectNameToPtr, node);  /* delete it (users advances to next) */
   } 
   HASH_ITER(hh_path, kb->objectPathToPtr, node, tmp) {
      HASH_DELETE(hh_path, kb->objectPathToPtr, node);  /* delete it (users advances to next) */
      freeTreeRootedAtNode(node);
      free(node->name);
      free(node->pathname);
      free(node);
   }
   HASH_ITER(hh, kb->classNameToPtr, cl, tmpcl) {
      HASH_DEL(kb->classNameToPtr, cl);  /* delete it (users advances to next) */
      freeTMLClass(cl); /* TMLClass owns name_and_ptr->name */
   }
   free(kb->classes);
   HASH_ITER(hh, kb->objToRelFactStrs, objRelHash, objRelTemp) {
      HASH_DEL(kb->objToRelFactStrs, objRelHash);
      HASH_ITER(hh, objRelHash->hash, relStrHash, relStrTemp) {
         HASH_DEL(objRelHash->hash, relStrHash);
         free(relStrHash->str);
         free(relStrHash);
      }
      free(objRelHash);
   }

   for (c = 0; c < kb->numClasses; c++) {
      qnode = kb->classToObjPtrs[c];
      while (qnode != NULL) {
         next = qnode->next;
         free(qnode);
         qnode = next;
      }
   }
   free(kb->classToObjPtrs);
   free(kb);
}

///////////////////////
// The following classes are used to print a new .db file
// based on evidence added in interactive mode
///////////////////////


void printSubclassesForObj(Node* obj, FILE* outFile) {
   int c;
   if (obj->cl->nsubcls == 0) return;
   if (obj->assignedSubcl != -1) {
         fprintf(outFile, ", %s", obj->cl->subcl[obj->assignedSubcl]->name);
         if (obj->subclMask == NULL)
            printSubclassesForObj(obj->subcl, outFile);
         else
            printSubclassesForObj(&(obj->subcl[obj->assignedSubcl]), outFile);
      return;
   }
   if (obj->subclMask == NULL) {
      for (c = 0; c < obj->cl->nsubcls; c++)
         printSubclassesForObj(&(obj->subcl[c]), outFile);
      return;
   }
   for (c = 0; c < obj->cl->nsubcls; c++) {
      if (obj->subclMask[c] != 1)
         fprintf(outFile, ", !%s", obj->cl->subcl[c]->name);
      else {
         printSubclassesForObj(&(obj->subcl[c]), outFile);
      }
   }
}

void printSubpartsForObj(Node* obj, FILE* outFile, int firstPart) {
   TMLPart* part;
   TMLPart* tmp;
   TMLClass* cl = obj->cl;
   int subcl = obj->assignedSubcl;
   int p, i;
   Node* nodePart;

   if (cl->nsubcls == 0 || subcl == -1) {
      p = 0;
      HASH_ITER(hh, cl->part, part, tmp) {
         for (i = 0; i < part->n; i++) {
            nodePart = obj->part[p][i];
            if (nodePart->name != NULL) {
               if (FALSE && part->n == 1) {
                  if (firstPart == 1) {
                     fprintf(outFile, "%s %s", nodePart->name, part->name, (i+1));
                     firstPart = 0;
                  } else
                     fprintf(outFile, ", %s %s", nodePart->name, part->name, (i+1));
               } else {
                  if (firstPart == 1) {
                     fprintf(outFile, "%s %s_%d", nodePart->name, part->name, (i+1));
                     firstPart = 0;
                  } else
                     fprintf(outFile, ", %s %s_%d", nodePart->name, part->name, (i+1));
               }
            }
         }
         p++;
      } 
      return;
   }
   p = 0;
   HASH_ITER(hh, cl->part, part, tmp) {
      if (subcl == -1 || part->defaultPart == 0 || part->defaultPartForSubcl[subcl] == 0) {
         for (i = 0; i < part->n; i++) {
            nodePart = obj->part[p][i];
            if (nodePart->name != NULL) {
               if (part->n == 1) {
                  if (firstPart == 1) {
                     fprintf(outFile, "%s %s", nodePart->name, part->name, (i+1));
                     firstPart = 0;
                  } else
                     fprintf(outFile, ", %s %s", nodePart->name, part->name, (i+1));
               } else {
                  if (firstPart == 1) {
                     fprintf(outFile, "%s %s_%d", nodePart->name, part->name, (i+1));
                     firstPart = 0;
                  } else
                     fprintf(outFile, ", %s %s_%d", nodePart->name, part->name, (i+1));
               }
            }
         }
      }
      p++;
   }
   if (obj->assignedSubcl != -1) {
      printSubpartsForObj(obj->subcl, outFile,firstPart);
      return;
   }
   if (obj->subclMask == NULL) {
      return;
   }
   for (i = 0; i < cl->nsubcls; i++) {
      if (obj->subclMask[i] == 1)
         printSubpartsForObj(&(obj->subcl[i]), outFile, firstPart);
   }
}

void printRelationsForObj(TMLKB* kb, Node* obj, FILE* outFile, int firstRel) {
   ObjRelStrsHash* objRelHash;
   RelationStr_Hash* relStrHash; 
   RelationStr_Hash* tmp; 
   char* namedStr;

   if (obj->pathname != NULL)
      HASH_FIND_STR(kb->objToRelFactStrs, obj->pathname, objRelHash);
   else
      HASH_FIND_STR(kb->objToRelFactStrs, obj->name, objRelHash);
   if (objRelHash != NULL) {
      HASH_ITER(hh, objRelHash->hash, relStrHash, tmp) {
         namedStr = createNamedRelStr(kb, obj, relStrHash->str);
         if (firstRel == 1) {
            fprintf(outFile, "%s%s", ((relStrHash->pol == 1) ? "" : "!"), namedStr);
            firstRel = 0;
         } else {
            fprintf(outFile, ", %s%s", ((relStrHash->pol == 1) ? "" : "!"), namedStr);
         }
         free(namedStr);
      }
   }
}

void printObject(TMLKB* kb, Node* node, FILE* outFile) {
   char* bestPath;

   if (node->name != NULL && strchr(node->name, '.') == NULL)
      fprintf(outFile, "%s {\n", node->name);
   else {
      bestPath = createBestPathname(kb, node);
      fprintf(outFile, "%s {\n", bestPath);
      free(bestPath);
   }

   fprintf(outFile, "%s", node->cl->name);
   printSubclassesForObj(node, outFile);
   fprintf(outFile, ";\n");

   printSubpartsForObj(node, outFile, 1);
   fprintf(outFile, ";\n");      

   printRelationsForObj(kb, node, outFile, 1);
   fprintf(outFile, ";\n");
   fprintf(outFile, "}\n\n"); 

   
}

void printTMLKBRec(TMLKB* kb, Node* node, FILE* outFile) {
   int p, i;
   TMLPart* part;
   TMLPart* tmp;

   if (node->cl->par == NULL)
      printObject(kb, node, outFile);

   p = 0; 
   HASH_ITER(hh, node->cl->part, part, tmp) {
      if (part->defaultPart == 0 || node->assignedSubcl == -1) {
         for (i = 0; i < part->n; i++) {
            printTMLKBRec(kb, node->part[p][i], outFile);
         }
      }
      p++;
   }
   if (node->cl->nsubcls != 0) {
      if (node->assignedSubcl != -1) {
         if (node->subclMask == NULL)
            printTMLKBRec(kb, node->subcl, outFile);
         else
            printTMLKBRec(kb, &(node->subcl[node->assignedSubcl]), outFile);
      }
   }
}

void printTMLKB(TMLKB* kb, const char* fileName) {
   char line[MAX_NAME_LENGTH+1];
   char* fileType = strrchr(fileName, '.');
   FILE* outFile;
   char db_fmt_str[50];
   Name_and_Ptr* name_and_ptr;
   Name_and_Ptr* name_and_ptr_tmp;
   Node* topNode;
   Node* node;
   Node* tmp;
   int c;
   char* bestPath;

   if (fileType == NULL || strcmp(fileType, ".db") != 0) {
      if (fileType != NULL && strcmp(fileType, ".") == 0) {
         printf("Filename cannot end with a '.'.\n");
         return;
      }
      snprintf(line, MAX_NAME_LENGTH, "%s.db", fileName);
      outFile = fopen(line, "w");
   } else {
      outFile = fopen(fileName, "w");
   }

   printTMLKBRec(kb, ((Node*)(kb->root->ptr)), outFile);
   fclose(outFile);
}

