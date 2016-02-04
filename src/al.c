#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "TMLClass.h"
#include "TMLKB.h"

#define MAX_LINE_LENGTH 10000
#define MAX_NAME_LENGTH 1000


int main(int argc, char *argv[]) { 
   TMLKB* kb;
   float logZ;
   float logZQ;
   int* id;
   TMLClass* cl;
   char inputBuffer[MAX_LINE_LENGTH];
   char question[2];
   char endline[2];
   char add_fmt_str[50];
   char query_fmt_str[50];
   char queryout_fmt_str[50];
   char print_fmt_str[50];
   char map_fmt_str[50];
   char em_fmt_str[50];
   char query[MAX_LINE_LENGTH+1];
   char outfile[MAX_LINE_LENGTH+1];
   char* p;
   Node* node;
   int correctScan;
   float initialLogZ;
   int a;
   int rulesIdx = -1;
   int evidIdx = -1;
   int queryIdx = -1;
   int outputIdx = -1;
   int map = -1;

   kb = TMLKBNew();
   if (argc < 3) {
      printf("Incorrect arguments to Alchemy Lite. Use flags:\n   -i     Rule file\n   -e     Fact file\n   -q     Query [If flag is not present and -map is not present, begins interactive mode]\n   -o     (Optional) Output file\n   -map   (Optional) Print MAP relations and classes\n");
      return;
   }
   for (a = 1; a < argc; a++) {
      if (strcmp(argv[a],"-i") == 0) {
         if (a == argc) {
            printf("Incorrect arguments to Alchemy Lite. Use flags:\n   -i     Rule file\n   -e     Fact file\n   -q     Query [If flag is not present and -map is not present, begins interactive mode]\n   -o     (Optional) Output file\n   -map   (Optional) Print MAP relations and classes\n");
            return;
         }
         if (rulesIdx != -1) {
            printf("Incorrect arguments to Alchemy Lite. Please specify only one rule file.\n");
            return;
         }
         rulesIdx = ++a;
      } else if (strcmp(argv[a],"-e") == 0) {
         if (a == argc) {
            printf("Incorrect arguments to Alchemy Lite. Use flags:\n   -i     Rule file\n   -e     Fact file\n   -q     Query [If flag is not present and -map is not present, begins interactive mode]\n   -o     (Optional) Output file\n   -map   (Optional) Print MAP relations and classes\n");
            return;
         }
         if (evidIdx != -1) {
            printf("Incorrect arguments to Alchemy Lite. Please specify only one fact file.\n");
            return;
         }
         evidIdx = ++a;
      } else if (strcmp(argv[a],"-q") == 0) {
         if (a == argc) {
            printf("Incorrect arguments to Alchemy Lite. Use flags:\n   -i     Rule file\n   -e     Fact file\n   -q     Query [If flag is not present and -map is not present, begins interactive mode]\n   -o     (Optional) Output file\n   -map   (Optional) Print MAP relations and classes\n");
            return;
         }
         if (queryIdx != -1) {
            printf("Incorrect arguments to Alchemy Lite. Please specify only one query. If more than one query is desired, please use interactive mode (by not specifying a query on the command line.\n");
            return;
         }
         queryIdx = ++a;
      } else if(strcmp(argv[a], "-o") == 0) {
         if (a == argc) {
            printf("Incorrect arguments to Alchemy Lite. Use flags:\n   -i     Rule file\n   -e     Fact file\n   -q     Query [If flag is not present and -map is not present, begins interactive mode]\n   -o     (Optional) Output file\n   -map   (Optional) Print MAP relations and classes\n");
            return;
         }
         if (outputIdx != -1) {
            printf("Incorrect arguments to Alchemy Lite. Please specify at most one output file.\n");
            return;
         }
         outputIdx = ++a;
      } else if (strcmp(argv[a], "-map") == 0) {
         map = 1;
      } else {
         printf("Incorrect arguments to Alchemy Lite. Use flags:\n   -i     Rule file\n   -e     Fact file\n   -q     Query [If flag is not present and -map is not present, begins interactive mode]\n   -o     (Optional) Output file\n   -map   (Optional) Print MAP relations and classes\n");
         return;
      }
   }
   if (queryIdx != -1 && map == 1) {
      printf("Please use either a query or MAP inference.\n");
      return;
   }
   snprintf(add_fmt_str, 50, "%%%d[^\r\n?)] %%1[)] %%1s", MAX_LINE_LENGTH);
   readInTMLRules(kb, argv[rulesIdx]);
   printf("Reading in .db file...\n");
   readInTMLFacts(kb, argv[evidIdx]);
   initialLogZ = fillOutSPN(kb, (Node*)(kb->root->ptr), ((Node*)(kb->root->ptr))->cl, kb->root->name);
   logZ = initialLogZ;
   printf("TML Knowledge Base successfully read in.\n");
   printf("   (Log of partition function Z is %f)\n", logZ);
   if (queryIdx != -1) {
      correctScan = sscanf(argv[queryIdx], add_fmt_str, query, question, endline);
      if (outputIdx == -1)
         computeQueryOrAddEvidence(kb, query, logZ, 1, NULL);
      else
         computeQueryOrAddEvidence(kb, query, logZ, 1, argv[outputIdx]);
   } else if (map == 1) {
      computeMAPState(kb, logZ);
      if (outputIdx == -1)
         printMAPState(kb, NULL);
      else
         printMAPState(kb, argv[outputIdx]);
   } else {
      snprintf(print_fmt_str, 50, " save %%%d[^\r\n]", MAX_NAME_LENGTH);
      snprintf(query_fmt_str, 50, " %%%d[^\r\n)?]) %%1[?] %%1s", MAX_NAME_LENGTH);
      snprintf(queryout_fmt_str, 50, " %%%d[^\r\n)?]) ? %%%d[^\r\n?)]", MAX_NAME_LENGTH, MAX_NAME_LENGTH);
      snprintf(map_fmt_str, 50, " MAP %%%d[^\r\n]", MAX_NAME_LENGTH);
      snprintf(em_fmt_str, 50, " EM %%%d[^\r\n]", MAX_NAME_LENGTH);
      printf("Welcome to the Alchemy Lite interactive prompt!\n");
      printf("    To add evidence, enter: <TMLFact>\n");
      printf("    To query the TML KB, enter: <Query>? [optionalOutputFilename]\n");
      printf("    To find the MAP state, enter: MAP [optionalOutputFilename]\n");
      printf("    To reset the TML KB, enter \"r\" or \"reset\"\n");
      printf("    To save the updated set of TML facts to .db file, enter: save <Filename>\n");
      printf("    To see these options again, enter: help\n");
      printf("    To quit, enter \"q\" or \"quit\"\n");
      printf("\n");
      while (1) {
         printf("> ");
         fflush(stdout);
         p = fgets(inputBuffer, MAX_LINE_LENGTH, stdin);
         if (strcmp(inputBuffer, "quit\n") == 0) break;
         if (strcmp(inputBuffer, "q\n") == 0) break;
         if (strcmp(inputBuffer, "\n") == 0) continue;
         if (strcmp(inputBuffer, "MAP\n") == 0
               || strcmp(inputBuffer, "map\n") == 0) {// DO MAP
            computeMAPState(kb, logZ);
            printMAPState(kb, NULL);
            continue;
         }
         if (strcmp(inputBuffer, "r\n") == 0) {
            resetKB(kb);
            logZ = initialLogZ;
            continue;
         }
         if (strcmp(inputBuffer, "reset\n") == 0) {
            resetKB(kb);
            logZ = initialLogZ;
            continue;
         }
         correctScan = sscanf(inputBuffer, map_fmt_str, query);
         if (correctScan == 1) {
            computeMAPState(kb, logZ);
            printMAPState(kb, query);
            kb->mapSet = 1;
            continue;
         }
         correctScan = sscanf(inputBuffer, print_fmt_str, query, endline);
         if (correctScan == 2) {
            // SAVE KB
            printTMLKB(kb, query);
            continue;
         }
         correctScan = sscanf(inputBuffer, queryout_fmt_str, query, outfile);
         if (correctScan == 2) {
            computeQueryOrAddEvidence(kb, query, logZ, 1, outfile);
            kb->mapSet = 0;
            continue;
         }
         correctScan = sscanf(inputBuffer, query_fmt_str, query, question, endline);
         if (correctScan == 2) {
            computeQueryOrAddEvidence(kb, query, logZ, 1, NULL);
            kb->mapSet = 0;
            continue;
         }
         correctScan = sscanf(inputBuffer, add_fmt_str, query, question, endline);
         if (correctScan == 2) {
            logZ = computeQueryOrAddEvidence(kb, query, logZ, 0, NULL);
            kb->mapSet = 0;
            continue;
         }
         if (!strcmp(inputBuffer, "help\n") == 0)
            printf("Malformed request\n");
         printf("    To add evidence, enter: <TMLFact>\n");
         printf("    To query the TML KB, enter: <Query>? [optionalOutputFilename]\n");
         printf("    To find the MAP state, enter: MAP [optionalOutputFilename]\n");
         printf("    To reset the TML KB, enter: reset\n");
         printf("    To save the updated TML KB to file, enter: save <Filename>\n");
         printf("    To quit, enter: quit\n");
         printf("\n");
      }
   }
   freeTMLKB(kb);
   return 0;
}
