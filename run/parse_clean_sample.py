import sys
import pandas


results_dict = dict()

results_dict["syscall number"] = list()
results_dict["eclog sample time"] = list()
results_dict["oplog sample time"] = list()
results_dict["rawec sample time"] = list()

eclog_sample_path = "clean_sample/eclog.out"
oplog_sample_path = "clean_sample/oplog.out"
rawec_sample_path = "clean_sample/rawec.out"


# overall
for path, target in zip([eclog_sample_path, oplog_sample_path, rawec_sample_path],
                        ["eclog", "oplog", "rawec"]):
    with open(f"{path}") as sample_output:
        lines = sample_output.readlines()
        for line in lines:
            split = line.split()
            if len(split) > 1 and split[0].isnumeric() and split[1].isnumeric():
                results_dict[f"{target} sample time"].append(int(split[1]))
                if target == "eclog":
                    results_dict["syscall number"].append(int(split[0]))

print(results_dict)
results_df = pandas.DataFrame(dict([(key, pandas.Series(value)) for key, value in results_dict.items()]))
with open("clean_sample.csv", "w") as csv_output:
    results_df.to_csv(csv_output, encoding='utf-8')
