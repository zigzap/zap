import re
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from collections import defaultdict
import statistics

directory = "./wrk"  # Replace with the actual directory path

requests_sec = defaultdict(list)
transfers_sec = defaultdict(list)

mean_requests = {}
mean_transfers = {}


def plot(kind='', title='', ylabel='', means=None):
    # Sort the labels and requests_sec lists together based on the requests_sec values
    labels = []
    values = []

    # silly, I know
    for k, v in means.items():
        labels.append(k)
        values.append(v)

    # sort the labels and value lists 
    labels, values = zip(*sorted(zip(labels, values), key=lambda x: x[1], reverse=True))

    # Plot the graph
    plt.figure(figsize=(10, 6))  # Adjust the figure size as needed
    bars = plt.bar(labels, values)
    plt.xlabel("Subject")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.xticks(rotation=45)  # Rotate x-axis labels for better readability

    # Display the actual values on top of the bars
    for bar in bars:
        yval = bar.get_height()
        plt.text(bar.get_x() + bar.get_width() / 2, yval, f'{yval:,.2f}', ha='center', va='bottom')

    plt.tight_layout()  # Adjust the spacing of the graph elements
    png_name = f"{directory}/{kind.lower()}_graph.png"
    plt.savefig(png_name)  # Save the graph as a PNG file
    print(f"Generated: {png_name}")


if __name__ == '__main__':
    if not os.path.isdir(".git"):
        print("Please run from root directory of the repository!")
        print("e.g. python wrk/graph.py")
        import sys
        sys.exit(1)

    # Iterate over the files in the directory
    for filename in os.listdir(directory):
        if filename.endswith(".perflog"):
            label = os.path.splitext(filename)[0]
            file_path = os.path.join(directory, filename)

            with open(file_path, "r") as file:
                lines = file.readlines()
                for line in lines: 
                    # Extract the Requests/sec value using regular expressions
                    match = re.search(r"Requests/sec:\s+([\d.]+)", line)
                    if match:
                        requests_sec[label].append(float(match.group(1)))
                    match = re.search(r"Transfer/sec:\s+([\d.]+)", line)
                    if match:
                        value = float(match.group(1))
                        if 'KB' in line:
                            value *= 1024
                        elif 'MB' in line:
                            value *= 1024 * 1024
                        value /= 1024.0 * 1024
                        transfers_sec[label].append(value)

    # calculate means
    for k, v in requests_sec.items():
        mean_requests[k] = statistics.mean(v)

    for k, v in transfers_sec.items():
        mean_transfers[k] = statistics.mean(v)

    # save the plots
    plot(kind='req_per_sec', title='Requests/sec Comparison',
         ylabel='requests/sec', means=mean_requests)
    plot(kind='xfer_per_sec', title='Transfer/sec Comparison',
         ylabel='transfer/sec [MB]', means=mean_transfers)
