#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""prep for the copy dir workloads.

"""

################################################################################
# common header

import os
import sys

CUR_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append('{}/..'.format(CUR_SCRIPT_DIR))
import gen_job_common as gjc

ENV_FTP_DIR_PREFIX = os.environ.get('FSP_PATH_PREFIX')
assert (ENV_FTP_DIR_PREFIX is not None)

################################################################################

# app-0
if not os.path.exists('a0'):
    os.mkdir('a0')

def prepend_name(orig_name: str)->str:
    return f'{ENV_FTP_DIR_PREFIX}{orig_name}'

def gen_path(name_list)->str:
    name_list = [prepend_name(n) for n in name_list]
    full_name = '/'.join(name_list)
    print(full_name)
    return full_name

FNAME = "t7"
t0_op_seq = gjc.ThreadOpSeq(aid=0, tid=0)
t0_op_seq.add_stat_op(FNAME)

# mkdirs
t0_op_seq.add_mkdir_op(gen_path(["src"]), 0o755)
t0_op_seq.add_mkdir_op(gen_path(["src", "dirl1_0"]), 0o755)
t0_op_seq.add_mkdir_op(gen_path(["src", "dirl1_1"]), 0o755)
t0_op_seq.add_mkdir_op(gen_path(["src", "dirl1_1", "dirl2_0"]), 0o755)
t0_op_seq.add_mkdir_op(gen_path(["src", "dirl1_1", "dirl2_1"]), 0o755)

# do writes
id0 = t0_op_seq.add_open_op(gen_path(["src", "dirl1_0", "f0"]), flags=(os.O_RDWR|os.O_CREAT))
str0 = gjc.gen_str_by_repeat('ab', 100000, 'z')
t0_op_seq.add_allocated_write_op(id0, str0, len(str0))
t0_op_seq.add_close_op(id0);

id1 = t0_op_seq.add_open_op(gen_path(["src", "dirl1_0", "f1"]), flags=(os.O_RDWR|os.O_CREAT))
str1 = gjc.gen_str_by_repeat('cd', 200000, 'y')
t0_op_seq.add_allocated_write_op(id1, str1, len(str1))
t0_op_seq.add_close_op(id1)

id2 = t0_op_seq.add_open_op(gen_path(["src", "dirl1_1", "f2"]), flags=(os.O_RDWR|os.O_CREAT))
str2 = gjc.gen_str_by_repeat('ef', 110000, 'x')
t0_op_seq.add_allocated_write_op(id2, str2, len(str2))
t0_op_seq.add_close_op(id2)

id3 = t0_op_seq.add_open_op(gen_path(["src", "dirl1_1", "dirl2_0", "f3"]), flags=(os.O_RDWR|os.O_CREAT))
str3 = gjc.gen_str_by_repeat('gh', 210000, 'y')
t0_op_seq.add_allocated_write_op(id3, str3, len(str3))
t0_op_seq.add_close_op(id3)

# done
t0_op_seq.finish()
cur_input_name = 'a0/{}'.format(gjc.get_input_json_leaf_fname(0))
t0_op_seq.gen_input_json('{}/{}'.format(CUR_SCRIPT_DIR, cur_input_name))
