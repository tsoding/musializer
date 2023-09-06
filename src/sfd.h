/**
 * Copyright (c) 2017 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `sfd.c` for details.
 */

#ifndef SFD_H
#define SFD_H

#define SFD_VERSION "0.1.0"

typedef struct {
  const char *title;
  const char *path;
  const char *filter_name;
  const char *filter;
  const char *extension;
} sfd_Options;

const char* sfd_get_error(void);
const char* sfd_open_dialog(sfd_Options *opt);
const char* sfd_save_dialog(sfd_Options *opt);

#endif