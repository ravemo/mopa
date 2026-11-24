#if 0
gcc $0 -o ${0%.*}
exit
#endif
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#define CMD_LEN 1024
#define GCC_FLAGS "-Wall", "-Wextra", "-ftabstop=1"

typedef enum {
	All,
	Main,
	Meta,
} Target;
int execCommand(const char* program, char *const cmd[]) {
	int cmd_len = 0;
	for(int i = 0; i < CMD_LEN; ++i) if(cmd[i] == NULL) {
		cmd_len = i;
		break;
	}
	assert(cmd[cmd_len] == NULL);
	printf("Executing '");
	for(int i = 0; i < cmd_len; ++i){
		printf("%s", cmd[i]);
		if(i < cmd_len-1) printf(" ");
	}
	printf("'\n");

	pid_t pid = fork();
	assert(pid != -1);
	if(pid == 0) { // is child
		execvp(program, cmd);
		exit(1);
	}

	int wstatus;
	assert(wait(&wstatus) != -1);
	if(WIFEXITED(wstatus)) {
		int exit_status = WEXITSTATUS(wstatus);
		fprintf(stderr, "Process exited with status %d\n", exit_status);
		return exit_status;
	} else if(WIFSIGNALED(wstatus)) {
		fprintf(stderr, "Process terminated with signal %d\n", WTERMSIG(wstatus));
		return -1;
	} else if(WIFSTOPPED(wstatus)) {
		fprintf(stderr, "Process stopped with signal %d\n", WSTOPSIG(wstatus));
		return -2;
	} else if(WIFCONTINUED(wstatus)) {
		fprintf(stderr, "Process continued.\n");
		return -3;
	} else {
		fprintf(stderr, "Something happened with the process.\n");
		return -4;
	}
}

int buildMain() {
	char* cmd[CMD_LEN] = {0};
	int cmd_len = 0;
	// TODO: Create function for splitting space-separated words into array
	cmd[cmd_len++] = "gcc";
	cmd[cmd_len++] = "-Wall";
	cmd[cmd_len++] = "-Wextra";
	cmd[cmd_len++] = "-Werror";
	cmd[cmd_len++] = "-ftabstop=1";
	cmd[cmd_len++] = "-ggdb3";
	cmd[cmd_len++] = "-O0";
	cmd[cmd_len++] = "-fno-omit-frame-pointer";
	DIR* cwd = opendir("src/");
	struct dirent* entry;
	char scratch[1 << 14];
	while((entry = readdir(cwd)) != NULL) {
		const char* name = entry->d_name;
		size_t n = strlen(entry->d_name);
		
		const char* source_dir = "src/";
		strcpy(scratch, source_dir);
		strcat(scratch, name);

		struct stat stbuf;
		if(stat(scratch, &stbuf) == -1) {
			fprintf(stderr, "Unable to stat file: %s\n", scratch);
			exit(1);
		}
		if(!S_ISREG(stbuf.st_mode)) continue;
		if (n < 2) continue;
		if (strcmp(name + n - 2, ".c") != 0) continue;
		cmd[cmd_len] = malloc(strlen(scratch)+1);
		strcpy(cmd[cmd_len], scratch);
		++cmd_len;
	}
	closedir(cwd);
	cmd[cmd_len++] = "-o";
	cmd[cmd_len++] = "build/mopa";
	cmd[cmd_len++] = "-lm";
	int status = execCommand("gcc", cmd);
	printf("Finished building project. (%d)\n\n", status);
	return status;
}

int buildAndExecuteMeta() {
	char* cmd[CMD_LEN] = { "gcc", GCC_FLAGS, "-ggdb3", "src/meta/meta.c", "-o", "src/meta/meta" };
	int status = execCommand("gcc", cmd);
	printf("Finished building meta program. (%d)\n\n", status);
	if(status) return status;
	char* cmd2[CMD_LEN] = { "src/meta/meta", "src/" };
	status = execCommand("src/meta/meta", cmd2);
	printf("Finished executing meta program. (%d)\n\n", status);
	return status;
}

int main(int argc, char** argv) {
	// TODO: Check if files are modified before compiling
	Target target = Main;
	for(int i = 1; i < argc; ++i){
		if(strcmp(argv[1], "all") == 0) target = All;
		if(strcmp(argv[1], "main") == 0) target = Main;
		if(strcmp(argv[1], "meta") == 0) target = Meta;
		// TODO: Do this automatically when modified and reload itself
	}
	int status;
	switch(target) {
		case All:
			if((status = buildAndExecuteMeta())) return status;
			if((status = buildMain())) return status;
			break;
		case Meta: return buildAndExecuteMeta();
		case Main: return buildMain();
	}
}
