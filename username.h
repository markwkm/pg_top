/* interface for username.c */

#ifndef _USERNAME_H_
#define _USERNAME_H_

void		init_hash();
char	   *username(int uid);
int			userid(char *username);

#endif   /* _USERNAME_H_ */
