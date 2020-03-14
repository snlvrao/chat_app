#define MAX_USR 100
struct message
{
	int online_count;
	struct list
	{
		char username[20];
		char qname[20];
		int status;
	}online_list[MAX_USR];
};
typedef struct message message;
typedef struct list list;
