#ifndef _NODE_H__
#define _NODE_H__

#include "TMLClass.h"
#include "uthash.h"
#include "util.h"

typedef struct TMLObject {
   char* name;
   QNode* paths;
   UT_hash_handle hh; /* makes this structure hashable */
} TMLObject;

/**
 * Node struct for SPN structure/
 * Each node represents a object:class pair
 */
typedef struct Node {
   // Class of the object
   TMLClass* cl;
   // Name of the object
   char* name;
   // Full path name of node
   char* pathname;
   // Parent nodes
   struct Node** par;
   int npars;
   // If we know finer class information for the object,
   // assignedSubcl is index into cl->subcl for the class. 
   // Otherwise -1.
   int assignedSubcl;
   // Array of nodes representing the object and subclass pairs
   struct Node* subcl;
   // part is array for each type of part in cl
   // part[i] is an array of Nodes representing the subparts
   // of type i of cl
   struct Node*** part;
   // relValues is an array for each relation type in cl
   // relValues[i] is an array of 3 int values counting how many
   // negative, positive, and unknown grounded relation facts for this
   // object are known.
   int** relValues;
   TMLAttrValue** assignedAttr;
   int** attrValues;
   // If not NULL, identifies which subclass branches are blocked
   // due to queries or !Is(X,C) facts
   int* subclMask;
   // Current value of the tree rooted at this Node
   float logZ;
   // Boolean stating whether or not the value of this tree has changed
   // due to a query.
   int changed;
   // maxSubcl is the index of the subclass with the highest weight
   // used for MAP inference
   int maxSubcl;
   int nmaxgroundliterals; // max number of ground literals in this branch
   UT_hash_handle hh; /* makes this structure hashable */
   UT_hash_handle hh_path; /* makes this structure hashable */
   int active; /* If 1, facts or names have been attributed to this node or a descendant. Otherwise 0.  */
} Node;
   
void initializeNode(Node* node, TMLClass* cl, char* name);
void freeTreeRootedAtNode(Node* node);
void addParent(Node* node, Node* par);
void markNodeAncestorsAsActive(Node* node);

TMLObject* newTMLObject(char* name);
void addPathToObject(TMLObject* obj, Node* node);
#endif
