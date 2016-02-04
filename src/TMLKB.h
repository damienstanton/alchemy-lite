#ifndef _TMLKB_H__
#define _TMLKB_H__

#include "Node.h"
#include "TMLClass.h"
#include "uthash.h"
#include "util.h"

/* Generic hash node structure for a string and a pointer to an object.
 */
typedef struct Name_and_Ptr {
   char* name;
   void* ptr;
   UT_hash_handle hh; /* makes this structure hashable */
} Name_and_Ptr;

typedef struct HashableInt {
   int i;
   char* key;
   UT_hash_handle hh;
} HashableInt;

typedef struct Hashable {
   void* ptr;
   UT_hash_handle hh; /* makes this structure hashable */
} Hashable;

/* Generic hash node structure for a string an a integer.
 */
typedef struct RelationStr_Hash {
   char* str;
   int pol;
   UT_hash_handle hh; /* makes this structure hashable */
} RelationStr_Hash;

typedef struct ObjRelStrsHash {
   char *obj;
   RelationStr_Hash* hash;
   UT_hash_handle hh; /* makes this structure hashable */
} ObjRelStrsHash;

typedef struct KBEdit {
   Node* node;
   char* relStr;
   int relIdx;
   int subclIdx;
   int valIdx;
   int pol;
   struct KBEdit* prev;
} KBEdit;

KBEdit* addKBEdit(Node* node, int subclIdx, char* relStr, int relIdx, int valIdx, int pol, KBEdit* prev);

/* Structure for a TML Knowledge Base
 */
typedef struct TMLKB {
   // The Top Class
   TMLClass* topcl;

   // The root of the SPN created from the KB and evidence
   // root->name is the name of the Top Object
   // root->ptr is the root node of the SPN
   Name_and_Ptr* root;

   // The number of classes and an array #numClasses TMLClass objects
   int numClasses;
   TMLClass* classes;
   // Hash table of class name to TMLClass objects. Uses the Name_and_Ptr
   // structure and ut_hash
   TMLClass* classNameToPtr;

   // Array of #numClasses hash tables. Each hash table contains Name_and_Ptr
   // structures that map object names to pointers to Nodes in the SPN.
   // Used for queries about whole classes of objects.
   QNode** classToObjPtrs;

   // Hash table mapping object names to Nodes in the SPN. Pointer will point to the
   // Node in the SPN for that object and its coarsest possible class.
   Node* objectNameToPtr;
   Node* objectPathToPtr;

   // Hash table mapping known relation facts to their polarity.
   // Strings are normalized to avoid differences in whitespace.
   ObjRelStrsHash* objToRelFactStrs;

   // The log of the partition function Z.
   float logZ;

   int mapSet;

   // Stack of edits made to the KB (used in interactive mode)
   KBEdit* edits;

   // Subclass pseudocount
   int scPct;

   // Relation pseudocounts
   int relPctT;
   int relPctF;

   // l0, l1 penalties
   float l0;
   float l1;

} TMLKB;

TMLKB* TMLKBNew();
void fillOutSubclasses(Node* node);
char* findBasePartName(char* str, int* num);
char* createBestPathname(TMLKB* kb, Node* node);
char* createNamedRelStr(TMLKB* kb, Node* node, char* normalizedStr);
char* createNormalizedRelStr(TMLKB* kb, char* relation, char* object, Node** argNodes, int nargs, int addBase, int useNames);
char* splitRelArgsAndCreateNormalizedRelStr(TMLKB* kb, char* relStr, char* relation, int* nargs, char*** args, int useNames);
Node* findNodeFromAnonName(TMLKB* kb, Node* base, const char* name, int init);
//Node* findNodeFromAnonName_TML1(TMLKB* kb, const char* name);
Node* initNodeToClass(TMLKB* kb, char* name, TMLClass* cl, TMLClass* finecl);
Node* initAnonNodeToClass(TMLClass* cl, char* name, TMLClass* finecl);
Node* updateClassForNodeRecHelper(TMLKB* kb, KBEdit** editPtr, char* name, Node* topNode, TMLClass* cl, TMLClass* finecl, FILE* tmlFactFile, int linenum);
Node* blockClassForNodeRecHelper(TMLKB* kb, KBEdit** editPtr, char* name, Node* topNode, TMLClass* cl, TMLClass* finecl, FILE* tmlFactFile, int linenum);
Node* updateClassForNode(TMLKB* kb, KBEdit** editPtr, char* name, Node* node, TMLClass* cl, FILE* tmlFactFile, int linenum);
Node* blockClassForNode(TMLKB* kb, KBEdit** editPtr, char* name, Node* node, TMLClass* cl, FILE* tmlFactFile, int linenum);
void addAndInitSubpartRecHelper(TMLKB* kb, Node* par, Node* obj, Node* subpart, char* part, int n, FILE* tmlFactFile, int linenum);
Node* addAndInitSubpart(TMLKB* kb, char* name, char* subpartname, Node* obj, char* part, int n, FILE* tmlFactFile, int linenum);
float computeLogZ(Node* node, TMLClass* assignedClassBySuperpart, float(*spn_func)(float* arr, int num, int* idx), int recompute);

Node* findPartUp(Node* node, const char* name, int n, int* maxParts);
void propagatePartUp(Node* node, Node* partNode, const char* name, int n);
Node* findAndInitPartDown(TMLKB* kb, Node* node, const char* name, int n, const char* anonStr);
Node* findPartDown(Node* node, const char* name, int n, int print, int* maxParts);
void renameNode(TMLKB* kb, Node* node, char* newName);
float fillOutSPN(TMLKB* kb, Node* node, TMLClass* assignedClassBySuperpart, char* anonName);
int checkForLostRoot(TMLClass* cl);
int readInOneTMLClass(TMLKB* kb, const char* className, TMLClass** rootCl, FILE* tmlRuleFile, int linenum, int id, int counts, int first);
int pushDefaultRelationDown(TMLKB* kb, TMLClass* cl, TMLRelation* rel);
void pushGroundLitCountDown(TMLClass* cl, TMLRelation* rel, int max);
void pushPartGroundLitCountDown(TMLClass* cl, TMLPart* part, int partCount, int maxParts);
void pushGroundLitCountUp(TMLClass* cl, int max);
void addPartGroundLitCount(TMLClass* cl);
int readInClassRelations(TMLKB* kb, TMLClass* cl, FILE* tmlRuleFile, char* line, int linenum, int counts);
void readInTMLRules(TMLKB* kb, const char* tmlRuleFileName);
Node* addClassEvidenceForObj(TMLKB* kb, char* objectName, Node* node, char* className, int pol, FILE* tmlFactFile, int linenum); 
void readInTMLFacts(TMLKB* kb, const char* tmlFactFileName);
TMLClass* getTopClass(TMLKB* kb);
void resetOneKBEdit(TMLKB* kb, KBEdit* edit);
void resetKBEdits(TMLKB* kb, KBEdit* edits);
void resetKB(TMLKB* kb);
void propagateKBChange(Node* node);
void propagateKBChangeUp(Node* node);
void propagateKBChangeDown(Node* node);
Node* findNodeForClass(Node* node, TMLClass* cl, const char* objName, const char* clName, FILE* outFile);
void changeRelationQuery(Node* node, const char* relStr, int add);
int blockClassesForPartRec(KBEdit** editsPtr, Node* node, TMLPart* part, int n, TMLClass* newClass);
Node* blockClassesForPartQuery(KBEdit** editsPtr, Node* par, Node* part, TMLClass* newClass);
void addRelationToKB(TMLKB* kb, Node* obj, const char* rel, int pol);
void addAttributeToKB(TMLKB* kb, Node* obj, const char* attrStr, const char* attrvalStr, int pol);
void removeRelationToKB(Node* obj, const char* rel, int pol);
void traverseSPNToFindParts(Node* node, TMLClass* cl, Name_and_Ptr** partHashPtr);
//int countDescendantSubpartsOfClass(TMLClass* cl, TMLClass* partcl);
void traverseKBToFindParts(TMLClass* cl, TMLClass* partcl, Name_and_Ptr** partHashPtr);
int computeNumRelationGroundings(Node* node, const char* relStr);
void computeRelationQueryOrAddEvidenceForObj(TMLKB* kb, Node* topNode, char* base, char* rel, char* rest, int pol, char* normalizedRelStr, char** args, int p, float logZ, int isQuery, int isClassQuery, char* outputRelStr, FILE* outFile);
void computeRelationQueryOrAddEvidence(TMLKB* kb, char* rel, char* iter, int pol, float logZ, int isQuery, FILE* outputFile);
void computeClassQueryForObject(TMLKB* kb, const char* objName, Node* obj, const char* clName, TMLClass* cl, float logZ, FILE* outputFile);
float computeQueryOrAddEvidence(TMLKB* kb, char* query, float logZ, int isQuery, const char* output);
void computeObjIndptQuery(TMLKB* kb, char* query, float logZ, int isQuery);
ArraysAccessor* createArraysAccessorForRel(TMLRelation* rel, Node* node);
void printMAPStateForObj(TMLKB* kb, Node* node, FILE* outFile);
void printMAPStateRec(TMLKB* kb, Node* node, TMLClass* assignedClFromSubpart, FILE* outFile);
void computeMAPState(TMLKB* kb, float logZ);
void testTraverseForClass(TMLKB* kb, const char* className);
TMLClass* addClass(TMLKB* kb, char* className, int id, int subclIdx);

TMLKB* readInTMLKBFromList(const char* tmlRuleFileName);

void freeTMLKB(void* obj);
void printSubclassesForObj(Node* obj, FILE* outFile);
void printSubpartsForObj(Node* obj, FILE* outFile, int firstPart);
void printRelationsForObj(TMLKB* kb, Node* obj, FILE* outFile, int firstRel);
void printObject(TMLKB* kb, Node* node, FILE* outFile);
void printTMLKB(TMLKB* kb, const char* fileName);

void setMAPCountsForObj(TMLKB* kb, Node* node);
void setMAPCounts(TMLKB* kb);
void resetMAPCountsForClass(TMLClass* cls);
void resetMAPCounts(TMLKB* kb);
void updateWtsForRel(TMLKB* kb, TMLRelation* rel);
void updateWtsForClass(TMLKB* kb, TMLClass* cls);
void updateWts(TMLKB* kb);
void learnWts(TMLKB* kb, float logZ);

#endif
