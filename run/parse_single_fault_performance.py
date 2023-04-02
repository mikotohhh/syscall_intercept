import sys
import pandas


results_dict = dict()
results_dict["fault_op_num"] = list()
results_dict["op_num"] = list()

results_dict["oplog save time"] = list()
results_dict["oplog recovery time"] = list()
results_dict["eclog save time"] = list()
results_dict["eclog recovery time"] = list()
results_dict["eclog overall time"] = list()
results_dict["oplog overall time"] = list()
results_dict["rawec overall time"] = list()

results_dict["eclog per op time"] = list()
results_dict["oplog per op time"] = list()
perop_faultop = 30000

eclog_detail_path = "eclog_large"
oplog_detail_path = "oplog_large"
eclog_overall_path = "eclog_large_overall"
oplog_overall_path = "oplog_large_overall"
rawec_overall_path = "rawec_large_overall"


# single op
for op_num in range(1000, 49000, 1000):
    results_dict["fault_op_num"].append(op_num)
    with open(f"{eclog_detail_path}/fault_op_num-{op_num}-0.out", "r") as eclog_fault_output:
        lines = eclog_fault_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "0:":
                results_dict["eclog save time"].append(int(split[2]))
    
    with open(f"{oplog_detail_path}/fault_op_num-{op_num}-0.out", "r") as oplog_fault_output:
        lines = oplog_fault_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "0:":
                results_dict["oplog save time"].append(int(split[2]))
        
    with open(f"{eclog_detail_path}/fault_op_num-{op_num}-1.out", "r") as eclog_recovery_output:
        lines = eclog_recovery_output.readlines()
        recovery_time = 0
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "1:":
                recovery_time += int(split[2])
            if len(split) > 3 and split[0] == "Timer" and split[1] == "2:":
                recovery_time += int(split[2])
        results_dict["eclog recovery time"].append(recovery_time)

    with open(f"{oplog_detail_path}/fault_op_num-{op_num}-1.out", "r") as oplog_recovery_output:
        lines = oplog_recovery_output.readlines()
        recovery_time = 0
        for line in lines:
            split = line.split()
            if len(split) > 3 and split[0] == "Timer" and split[1] == "1:":
                recovery_time += int(split[2])
            if len(split) > 3 and split[0] == "Timer" and split[1] == "2:":
                recovery_time += int(split[2])
        results_dict["oplog recovery time"].append(recovery_time)

# overall
for path, target in zip([eclog_overall_path, oplog_overall_path, rawec_overall_path],
                        ["eclog", "oplog", "rawec"]):
    with open(f"{path}/timer.out") as overall_output:
        lines = overall_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 4 and split[2] == "Time:":
                results_dict[f"{target} overall time"].append(int(split[3]))

# per op latency
op_num_recorded = False
for path, target in zip([eclog_overall_path, oplog_overall_path],
                        ["eclog", "oplog"]):
    with open(f"{path}/fault_op_num-{perop_faultop}-app.out") as app_output:
        lines = app_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 4 and split[2] == "Time:":
                results_dict[f"{target} per op time"].append(int(split[3]))
                if not op_num_recorded:
                    results_dict["op_num"].append(int(split[1]))
    op_num_recorded = True


print(results_dict)
results_df = pandas.DataFrame(dict([(key, pandas.Series(value)) for key, value in results_dict.items()]))
with open("single_fault_performance_complete.csv", "w") as csv_output:
    results_df.to_csv(csv_output, encoding='utf-8')
