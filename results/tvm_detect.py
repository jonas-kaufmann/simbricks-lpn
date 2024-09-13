"""This script shows the inference result of
vta/tutorials/frontend/deploy_detection.py."""

from io import BytesIO
import sys
import matplotlib.pyplot as plt
import json
import base64

figure = 1


def extract_inference_result(host: str, stdout: list[str]):
    start = None
    end = None
    for i in range(len(stdout)):
        if stdout[i].startswith("dump deploy_detection-infer-result.png START"):
            start = i + 2
        elif stdout[i].startswith("dump deploy_detection-infer-result.png END"):
            end = i - 1
            break
    if start is None:
        raise RuntimeError(f"Start for host {host} couldn't be found")
    if end is None:
        raise RuntimeError(f"End for host {host} couldn't be found")

    lines = stdout[start:end]
    lines = [line.removesuffix("\r") for line in lines]
    encoded_b64 = "".join(lines)
    decoded = base64.b64decode(encoded_b64, validate=True)
    img = plt.imread(BytesIO(decoded))
    return img


def render_result(img, host: str) -> None:
    global figure
    plt.figure(figure)
    figure += 1
    plt.title(f"Inferred Image on Host {host}")
    plt.imshow(img)


def main():
    if len(sys.argv) != 2:
        print("Usage: tvm_detect.py experiment_output.json")
        sys.exit(1)

    with open(sys.argv[1], mode="r", encoding="utf-8") as file:
        exp_out = json.load(file)
        sims = exp_out["sims"]
        hosts = [
            sim for sim in sims.keys() if sim.startswith("host.tvm_client.")
        ]

        for host in hosts:
            img = extract_inference_result(host, sims[host]["stdout"])
            render_result(img, host)

    plt.show()


if __name__ == "__main__":
    main()
