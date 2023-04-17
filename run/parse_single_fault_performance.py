import sys
import pandas


results_dict = dict()
results_dict["fault_op_num"] = list()
results_dict["syscall_number"] = list()
results_dict["opcode"] = list()

results_dict["oplog save time"] = list()
results_dict["oplog recovery time"] = list()
results_dict["oplog restart time"] = list()
results_dict["eclog save time"] = list()
results_dict["eclog recovery time"] = list()
results_dict["eclog restart time"] = list()
results_dict["rawec restart time"] = list()

results_dict["eclog overall time"] = list()
results_dict["oplog overall time"] = list()
results_dict["rawec overall time"] = list()
results_dict["rawec overall time2"] = list()

results_dict["eclog per op time"] = list()
results_dict["oplog per op time"] = list()
results_dict["rawec per op time"] = list()

eclog_overall_path = "eclog_final2"
oplog_overall_path = "oplog_final3"
rawec_overall_path = "rawec_final3"


# single op
for op_num in range(499, 48001, 499):
    results_dict["fault_op_num"].append(op_num)
    with open(f"{eclog_overall_path}/fault_op_num-{op_num}-0.out", "r") as eclog_fault_output:
        recovery_time = 0
        lines = eclog_fault_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "0:":
                results_dict["eclog save time"].append(int(split[2])*1000)
            if len(split) > 3 and split[0] == "Timer" and split[1] == "1:":
                recovery_time += int(split[2])*1000
            if len(split) > 3 and split[0] == "Timer" and split[1] == "2:":
                recovery_time += int(split[2])*1000
            if len(split) > 3 and split[0] == "Timer" and split[1] == "3:":
                results_dict["eclog restart time"].append(int(split[2])*1000)
        results_dict["eclog recovery time"].append(recovery_time)
    
    with open(f"{oplog_overall_path}/fault_op_num-{op_num}-0.out", "r") as oplog_fault_output:
        recovery_time = 0
        lines = oplog_fault_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "0:":
                results_dict["oplog save time"].append(int(split[2])*1000)
            if len(split) > 3 and split[0] == "Timer" and split[1] == "1:":
                recovery_time += int(split[2])*1000
            if len(split) > 3 and split[0] == "Timer" and split[1] == "2:":
                recovery_time += int(split[2])*1000
            if len(split) > 3 and split[0] == "Timer" and split[1] == "3:":
                results_dict["oplog restart time"].append(int(split[2])*1000)
        results_dict["oplog recovery time"].append(recovery_time)

    with open(f"{rawec_overall_path}/fault_op_num-{op_num}-0.out", "r") as rawec_fault_output:
        lines = rawec_fault_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "3:":
                results_dict["rawec restart time"].append(int(split[2])*1000)
            if len(split) > 6 and split[2] == "OPCODE:":
                results_dict["opcode"].append(int(split[3]))
        
    # with open(f"{eclog_detail_path}/fault_op_num-{op_num}-1.out", "r") as eclog_recovery_output:
    #     lines = eclog_recovery_output.readlines()
    #     recovery_time = 0
    #     for line in lines:
    #         split = line.split()
    #         if len(split) > 3 and split[0] == "Timer" and split[1] == "1:":
    #             recovery_time += int(split[2])
    #         if len(split) > 3 and split[0] == "Timer" and split[1] == "2:":
    #             recovery_time += int(split[2])
    #     results_dict["eclog recovery time"].append(recovery_time)

    # with open(f"{oplog_detail_path}/fault_op_num-{op_num}-1.out", "r") as oplog_recovery_output:
    #     lines = oplog_recovery_output.readlines()
    #     recovery_time = 0
    #     for line in lines:
    #         split = line.split()
    #         if len(split) > 3 and split[0] == "Timer" and split[1] == "1:":
    #             recovery_time += int(split[2])
    #         if len(split) > 3 and split[0] == "Timer" and split[1] == "2:":
    #             recovery_time += int(split[2])
    #     results_dict["oplog recovery time"].append(recovery_time)

# overall
# for path, target in zip([eclog_overall_path, oplog_overall_path],
#                         ["eclog", "oplog"]):
#     with open(f"{path}/timer.out", "r") as overall_output:
#         lines = overall_output.readlines()
#         for line in lines:
#             split = line.split()
#             if len(split) > 4 and split[2] == "SecondTime:":
#                 results_dict[f"{target} overall time"].append(int(split[3]))

# for path, target in zip([rawec_overall_path],
#                         ["rawec"]):
#     with open(f"{path}/timer.out", "r") as overall_output:
#         lines = overall_output.readlines()
#         for line in lines:
#             split = line.split()
#             if len(split) > 4 and split[2] == "FirstTime:":
#                 results_dict[f"{target} overall time1"].append(int(split[3]))
#             if len(split) > 4 and split[2] == "SecondTime:":
#                 results_dict[f"{target} overall time2"].append(int(split[3]))


# overall
for path, target in zip([eclog_overall_path, oplog_overall_path, rawec_overall_path],
                        ["eclog", "oplog", "rawec"]):
    for op_num in range(499, 48001, 499):
        with open(f"{path}/fault_op_num-{op_num}-app.timer", "r") as overall_output:
            lines = overall_output.readlines()
            for line in lines:
                split = line.split()
                if len(split) > 2 and split[0] == "Time:":
                    results_dict[f"{target} overall time"].append(int(split[1]))
        if path == rawec_overall_path:
            with open(f"{path}/fault_op_num-{op_num}-app.timer2", "r") as overall_output:
                lines = overall_output.readlines()
                for line in lines:
                    split = line.split()
                    if len(split) > 2 and split[0] == "Time:":
                        results_dict[f"{target} overall time2"].append(int(split[1]))

# per op
temp_list = list()
sysnum_recorded = False
for target in ["eclog", "oplog", "rawec"]:
    temp_list.clear()
    with open(f"single/{target}_single_final.app", "r") as app_output:
        lines = app_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 5 and split[2] == "single:":
                temp_list.append((int(split[5]), int(split[3])))
        temp_list.sort(key=lambda k: k[0])
        for item in temp_list:
            if not sysnum_recorded:
                results_dict["syscall_number"].append(item[0])
            results_dict[f"{target} per op time"].append(item[1])
    sysnum_recorded = True


print(results_dict)
results_df = pandas.DataFrame(dict([(key, pandas.Series(value)) for key, value in results_dict.items()]))
with open("cp_final2.csv", "w") as csv_output:
    results_df.to_csv(csv_output, encoding='utf-8')
