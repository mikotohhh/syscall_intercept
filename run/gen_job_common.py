#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Common functions for generating jobs.

"""

import json
import math


def gen_str_scheme1(l: int) -> str:
    cur_str = 'ab' * (l / 2) + 'xyz'
    return cur_str[0:l + 1]


def gen_str_by_repeat(seed_str: str, l: int, tail=None) -> str:
    seed_len = len(seed_str)
    ret_str = (seed_str * math.ceil(l / seed_len))[0:l]
    if tail is not None and len(tail) > 0:
        ret_str = ret_str[0:l - len(tail)] + tail
    return ret_str


def get_input_json_leaf_fname(tid: int) -> str:
    fname = '{}.input.json'.format(tid)
    return fname


def gen_open_flag(flag_str) -> int:
    if flag_str == 'O_APPEND':
        return int(2**10)
    if flag_str == 'O_CREAT':
        return int(2**6)
    if flag_str == 'O_RDONLY':
        return int(0)
    if flag_str == 'O_WRONLY':
        return int(2**0)
    if flag_str == 'O_RDWR':
        return int(2**1)
    raise RuntimeError('flag:{} not supported'.format(flag_str))


class ThreadOpSeq(object):

    def __init__(self, aid, tid):
        self.aid = aid
        self.tid = tid
        self.op_dict = {}
        self.op_id_incr = 0
        self.num_op = None

    def finish(self) -> int:
        self.num_op = len(self.op_dict.keys())
        self.op_dict["num"] = self.num_op
        assert (self.num_op == self.op_id_incr)
        return self.num_op

    def gen_input_json(self, fname: str):
        with open(fname, 'w') as outfile:
            json.dump(self.op_dict, outfile, indent=4)

    def _add_op_common(self, op_name):
        if self.num_op is not None:
            raise RuntimeError("cannot add op after finish")
        cur_op_id = self.op_id_incr
        self.op_id_incr += 1
        self.op_dict[cur_op_id] = {"op": op_name, "ret": 0}
        return cur_op_id, self.op_dict[cur_op_id]

    def _set_fd_arg_common(self, cur_op_dict, ret_of_op_i_as_fd):
        assert (ret_of_op_i_as_fd in self.op_dict)
        assert (self.op_dict[ret_of_op_i_as_fd]["op"] == "open")
        cur_op_dict["fd"] = "%{}".format(ret_of_op_i_as_fd)

    # int stat(const char *pathname, struct stat *statbuf);
    def add_stat_op(self, pathname: str):
        cur_op_id, cur_op_dict = self._add_op_common("stat")
        cur_op_dict["pathname"] = pathname
        cur_op_dict["statbuf"] = 0
        return cur_op_id

    # int open(const char *pathname, int flags);
    def add_open_op(self, pathname: str, flags: int, mode=None):
        cur_op_id, cur_op_dict = self._add_op_common("open")
        cur_op_dict["pathname"] = pathname
        cur_op_dict["flags"] = flags
        if mode is not None:
            cur_op_dict["mode"] = mode
        return cur_op_id

    def add_mkdir_op(self, pathname:str, mode:int):
        cur_op_id, cur_op_dict = self._add_op_common("mkdir")
        cur_op_dict["pathname"] = pathname
        cur_op_dict["mode"] = mode
        return cur_op_id

    # ssize_t pread(int fd, void *buf, size_t count, off_t offset);
    def add_allocated_pread_op(self, ret_of_op_i_as_fd: int, count: int,
                               offset: int):
        cur_op_id, cur_op_dict = self._add_op_common("allocated_pread")
        self._set_fd_arg_common(cur_op_dict, ret_of_op_i_as_fd)
        cur_op_dict["buf"] = 0
        cur_op_dict["count"] = count
        cur_op_dict["offset"] = offset
        return cur_op_id

    # ssize_t read(int fd, void *buf, size_t count);
    def add_allocated_read_op(self, ret_of_op_i_as_fd: int, count: int):
        cur_op_id, cur_op_dict = self._add_op_common("allocated_read")
        self._set_fd_arg_common(cur_op_dict, ret_of_op_i_as_fd)
        cur_op_dict["buf"] = 0
        cur_op_dict["count"] = count
        return cur_op_id

    # ssize_t write(int fd, const void *buf, size_t count);
    def add_allocated_write_op(self, ret_of_op_i_as_fd: int, buf: str,
                               count: int):
        cur_op_id, cur_op_dict = self._add_op_common("allocated_write")
        self._set_fd_arg_common(cur_op_dict, ret_of_op_i_as_fd)
        cur_op_dict["buf"] = buf
        cur_op_dict["count"] = count
        assert (len(buf) == count)
        return cur_op_id

    # ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
    def add_allocated_pwrite_op(self, ret_of_op_i_as_fd: int, buf: str,
                                count: int, offset: int):
        cur_op_id, cur_op_dict = self._add_op_common("allocated_pwrite")
        self._set_fd_arg_common(cur_op_dict, ret_of_op_i_as_fd)
        cur_op_dict["buf"] = buf
        cur_op_dict["count"] = count
        assert (len(buf) == count)
        cur_op_dict["offset"] = offset
        return cur_op_id

    # int close(int fd);
    def add_close_op(self, ret_of_op_i_as_fd: int):
        cur_op_id, cur_op_dict = self._add_op_common("close")
        self._set_fd_arg_common(cur_op_dict, ret_of_op_i_as_fd)
        return cur_op_id

    # int unlink(const char *pathname);
    def add_unlink_op(self, pathname: str):
        cur_op_id, cur_op_dict = self._add_op_common("unlink")
        cur_op_dict["pathname"] = pathname
        return cur_op_id

    def add_fdatasync_op(self, ret_of_op_i_as_fd: int):
        cur_op_id, cur_op_dict = self._add_op_common("fdatasync")
        self._set_fd_arg_common(cur_op_dict, ret_of_op_i_as_fd)
        return cur_op_id
