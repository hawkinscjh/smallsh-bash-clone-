// Name: Casey Hawkins
// Date: 02/26/2023
// Class: OSU_cs344_400_w2023
// Description: smallsh: implement own shell in C. smallsh implements a command line interface similar to well-known shells, such as bash.

#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <ctype.h>

/* Max amount of arguments to pass in each argument */
#define MAX_WORDS 512

/* Global variables */
int status = 0;             // expands from $? symbol
int bgPid = 0;              // expands from $!
int background = 0;         // track if process is running in background
int bgProcesses[10] = {0};  // track background process pids in list
int bgCounter = 0;          // track number of background processes
int backgroundStatus = 0;   // track status of most recent completed background status
int childExitMethod;        // initialize child exit status
pid_t spawnPid;             // initialize spawnPid
char *inputFile;            // input file for redirection
char *outputFile;           //output file for redirection
int inputRedirect = 0;      // track if input will be redirected
int outputRedirect = 0;     // track if output will be redirected
char **parsedArguments;     // final array of arguments to be processed kept in pointer to array of pointers

/* Function prototypes */
char *expansion(char *userInput);
int changeDirectory(char *arg);
int runInput(int *argc, char *argv[]);
void getInput(int *argc, char *argv[]);
int exitShell(int *argc, char *argv[]);
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
void handleSIGINT(int signo);
void ignoreSIGTSTP();
void ignoreSIGINT();
void defaultSIGINT();
void handleSIGTSTP();
void handleSIGSTOP(int signo);
char *parseArguments(int *argc, char *userInput);
void checkBackgroundProcesses();

/* Ignore Ctrl-Z (SIGTSTP) signals */
void ignoreSIGTSTP()
{
  struct sigaction ignoreSIGTSTP = {0};
  ignoreSIGTSTP.sa_handler = SIG_IGN;
  sigfillset(&ignoreSIGTSTP.sa_mask);
  ignoreSIGTSTP.sa_flags = 0;
  sigaction(SIGTSTP, &ignoreSIGTSTP, NULL);
}

/* Ignore Ctrl-C (SIGINT) signals */
void ignoreSIGINT()
{
  struct sigaction sa_SIGINT = {0};
  sa_SIGINT.sa_handler = SIG_IGN;
  sigfillset(&sa_SIGINT.sa_mask);
  sa_SIGINT.sa_flags = 0;
  sigaction(SIGINT, &sa_SIGINT, NULL);
}

/* Reset SIGINT signals to default behavior */
void defaultSIGINT()
{
  struct sigaction sa_SIGINT = {0};
  sa_SIGINT.sa_handler = SIG_DFL;
  sigfillset(&sa_SIGINT.sa_mask);
  sa_SIGINT.sa_flags = 0;
  sigaction(SIGINT, &sa_SIGINT, NULL);
}

/* Handle received SIGINT signals by killing background process */
void handleSIGINT(int signo)
{
  if (bgPid != 0)
  {
    kill(bgPid, SIGINT);
    fprintf(stderr, "Child process %d done. Signaled %d.\n", bgPid, signo);
  }
  fflush(stdout);
}

/* Handle received SIGSTOP signals */
void handleSIGSTOP(int signo)
{
  fprintf(stderr, "Child process %d stopped. Continuing.\n", bgPid);
  fflush(stdout);
}

int main()
{
	int argc;
	char *argv[MAX_WORDS];

  ignoreSIGTSTP();
  ignoreSIGINT(); 

  for(;;)
  {
    argc = 0;
    status = 0;
    background = 0;
    inputRedirect = 0;
    outputRedirect = 0;
    memset(argv, '\0', MAX_WORDS);
    fflush(stdout);
    fflush(stdin);
    checkBackgroundProcesses();
    getInput(&argc, argv);
	}

	return 0;
}


/* Check for completed background processes */
void checkBackgroundProcesses()
{
  for (int i = 0; i < bgCounter; i++)
  {
    if (waitpid(bgProcesses[i], &childExitMethod, WNOHANG) > 0)
    {
      if (WIFSIGNALED(childExitMethod))
      {
        fprintf(stderr, "Child processes %d done. Signaled %d.\n", bgProcesses[i], WTERMSIG(childExitMethod));
      }
      if (WIFEXITED(childExitMethod))
      {
        fprintf(stderr, "Child process %d done. Exit status %d.\n", bgProcesses[i], WEXITSTATUS(childExitMethod));
      }
    }
  }
}

/* Exit program command
 * Can be form "exit" or "exit (int)"
 * Sends SIGINT signal to all processes
 * Raise error for passing too many arguments
 * or non-integer as 2nd argument */
int exitShell(int *argc, char *argv[])
{
  if (*argc == 1)
  {
    fflush(stdout);
    fflush(stdin);
    fprintf(stderr, "\nexit\n");
    kill(0, SIGINT);
    exit(status);
  }
  if (*argc > 2)
  {
    fprintf(stderr, "smallsh: too many arguments\n");
    *argc = 0;
    return 0;
  }
  if (*argc == 2 && (isdigit(*argv[1]) == 0))
  {
    fprintf(stderr, "smallsh: second command must be integer\n");
    *argc = 0;
    return 0;
  }
  else if (*argc == 2 && (isdigit(*argv[1]) != 0))
  {
    status = atoi(argv[1]);
    fflush(stdout);
    fflush(stdin);
    fprintf(stderr, "\nexit %d\n", status);
    kill(0, SIGINT);
    exit(status);
  }

  return status;
}

/* Fork processes, redirect input/output in child process, execute non-built-in commands
 * Track most recent background pid and status of last foreground command */
int runInput(int *argc, char *argv[])
{
  
  int fd;
  
  pid_t spawnPid = fork();
  
  switch (spawnPid) {

    // Child process failure
    case -1:
            fprintf(stderr, "smallsh: fork() process error\n");
            exit(EXIT_FAILURE);
            break;

    // Child process
    case 0:
            defaultSIGINT();
            if (inputRedirect == 1)
            {
              inputRedirect = 0;
              fd = open(inputFile, O_RDONLY);
              if (fd < 0)
              {
                fprintf(stderr, "smallsh: unable to open file %s\n", inputFile);
                exit(EXIT_FAILURE);
              }

              int result = dup2(fd, 0);
              if (result == -1)
              {
                fprintf(stderr, "smallsh: input file error\n");
                exit(EXIT_FAILURE);
              }
            }

            if (outputRedirect == 1)
            {
              outputRedirect = 0;
              fd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0777);

              if (fd < 0)
              {
                fprintf(stderr,"smallsh: unable to open file %s\n", outputFile);
                exit(EXIT_FAILURE);
              }
             
              int result = dup2(fd, 1);
              if (result == -1)
              {
                fprintf(stderr, "smallsh: output file error\n");
                exit(EXIT_FAILURE);
              }
            }
            execvp(argv[0], argv);
            fprintf(stderr, "smallsh: execv error\n");
            exit(EXIT_FAILURE);
            break;
    // Parent process
    default:
            if (background == 1)
            {
              bgProcesses[bgCounter] = spawnPid;
              bgCounter++;
              waitpid(spawnPid, &childExitMethod, WNOHANG);
              background = 0;
              bgPid = spawnPid;
              fflush(stdout);
              return spawnPid;
            }
            else
            {
              waitpid(spawnPid, &childExitMethod, 0);
              if (WIFEXITED(childExitMethod))
              {
                status = WEXITSTATUS(childExitMethod);
              }
              return status;
            }
  }
  return 0;
}


/* Built-in command: change working directory */
int changeDirectory(char *dir){
  if (!dir) {
    dir = getenv("HOME");
  }
  
  status = chdir(dir);
  if (status)
  {
    fprintf(stderr, "smallsh: change directory error\n");
  }

  return status;
}


/* Expand any variables to their respective values
 * ~/ expands to HOME directory location
 * $$ expands to getpid()
 * $? expands to status of most recent foreground command
 * $! expands to pid of most recent background process */
char *expansion(char *userInput)
{
  int pid = getpid();

  char pidStr[10];
  sprintf(pidStr, "%d", pid);

  char *ret2 = str_gsub(&userInput, "~/", "~//");
  if (!ret2) exit(1);
  userInput = ret2;
  char *ret2_1 = str_gsub(&userInput, "~/", getenv("HOME"));
  if (!ret2_1) exit(1);
  userInput = ret2_1;

  char *ret1 = str_gsub(&userInput, "$$", pidStr);
  if (!ret1) exit(1);
  userInput = ret1;
  
  char statusStr[10];
  sprintf(statusStr, "%d", status);
	char *ret3 = str_gsub(&userInput, "$?", statusStr);
	if (!ret3) exit(1);
	userInput = ret3;

  char bgPidStr[10];
  if (bgPid != 0)
  {
    sprintf(bgPidStr, "%d", bgPid);
  }
  else
  {
    sprintf(bgPidStr, "%s", "");
  }
	char *ret4 = str_gsub(&userInput, "$!", bgPidStr);
	if (!ret4) exit(1);
	userInput = ret4;

  return userInput;
}


/* Parse arguments passed in userInput into array parsedArguments
 * delimited by IFS environmental variable
 * Tracks redirection operators and input/output files ">" and "<"
 * Tracks if command will be ran in the background "&"
 * Tracks if command is part of a comment "#" */
char *parseArguments(int *argc, char *userInput)
{
  char *tok_delim = getenv("IFS");
  if (tok_delim == NULL)
  {
    tok_delim = " \t\n";
  }

  parsedArguments = calloc(513, sizeof(char *));

  char *token = strtok(userInput, tok_delim);

  while (token != NULL)
  {
    if (strcmp(token, ">") == 0 || strcmp(token, "<") == 0)
    {
      char operator = token[0];

      token = strtok(NULL, tok_delim);
      if (!token) {
        fprintf(stderr, "smallsh: no file to redirect input/output\n");
        exit(EXIT_FAILURE);
      }

      if (operator == '>') {
        outputRedirect = 1;
        outputFile = strdup(token);
      } else 
      {
	      inputRedirect = 1;
        inputFile = strdup(token);
      }

      token = strtok(NULL, tok_delim);
      continue;
    } else if (strcmp(token, "&") == 0) 
    {
      background = 1;
      token = strtok(NULL, tok_delim);
      continue;
    } else if (strcmp(token, "#") == 0)
    {
      break;
    }
    parsedArguments[*argc] = strdup(token);
    (*argc)++;
    token = strtok(NULL, tok_delim);
  }
  return *parsedArguments;
}


/* Get input from user in getline() function
 * Expand userInput and parses arguments into parsedArguments array
 * Executes built-in commands first, then non-built-in commands by calling
 * runInput */
void getInput(int *argc, char *argv[])
{

  enter_prompt:
  setbuf(stdout, NULL);
  char *userInput = NULL;
  size_t n = 0;
  char *prompt = getenv("PS1");
  if (prompt == NULL)
  {
    prompt = "";
  }
  
  fprintf(stdout, "%s", prompt);

  struct sigaction sa_SIGINT = {0};
  sa_SIGINT.sa_handler = handleSIGINT;
  sigfillset(&sa_SIGINT.sa_mask);
  sa_SIGINT.sa_flags = 0;
  sigaction(SIGINT, &sa_SIGINT, NULL);

  getline(&userInput, &n, stdin);
  int length = strlen(userInput);
  if (userInput[0] == '\0' || length <= 1)
  {
    fflush(stdin);
    fflush(stdout);
    goto enter_prompt;
  }

  ignoreSIGINT();

  userInput = expansion(userInput);
  parseArguments(argc, userInput);

  if (strcmp(parsedArguments[0], "exit") == 0) 
  {
    status = exitShell(argc, parsedArguments);
  } else if (strcmp(parsedArguments[0], "cd") == 0) 
  {
    status = changeDirectory(parsedArguments[1]);
  } else
  {
    status = runInput(argc, parsedArguments);
  }
}


/* Search and replace function from RG's Youtube video */
/* Needle and haystack function used in expansion() function */
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub)
{
    char *str = *haystack;
    size_t haystack_len = strlen(str);
    size_t const needle_len = strlen(needle), sub_len = strlen(sub);

    for (; (str = strstr(str, needle));)
    {
        ptrdiff_t off = str - *haystack;
        if (sub_len > needle_len)
        {
            str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
            if (!str) goto exit;
            *haystack = str;
            str = *haystack + off;
        }

        memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
        memcpy(str, sub, sub_len);
        haystack_len = haystack_len + sub_len - needle_len;
        str += sub_len;
    }
    str = *haystack;
    if (sub_len < needle_len) 
    {
        str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
        if (!str) goto exit;

        *haystack = str;
    }
exit:
    return str;
}

