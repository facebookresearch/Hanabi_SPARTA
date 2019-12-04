FROM nvidia/cuda:10.0-cudnn7-devel
EXPOSE 3000 8080 5000


RUN apt-get update
RUN apt-get install -y software-properties-common
RUN apt-get -y install build-essential
RUN apt-get install -y unzip
RUN apt-get install -y wget
RUN apt-get install -y vim
RUN add-apt-repository ppa:mhier/libboost-latest
RUN apt update
RUN apt install -y libboost1.68-dev

RUN apt-get update
RUN apt-get install -y python3-pip
RUN apt-get -y install python3-setuptools

WORKDIR /
RUN wget -q https://download.pytorch.org/libtorch/cu100/libtorch-shared-with-deps-latest.zip
RUN unzip -q libtorch-shared-with-deps-latest.zip

RUN pip3 install torch==1.2.0
RUN apt-get install -y zlib1g-dev
RUN pip3 install quart

COPY . /home/game
WORKDIR /home/game

ENV SEARCH_THRESH 0.1
ENV SEARCH_BASELINE 1
ENV PARTNER_UNC 0.2
ENV SEARCH_N 10000
ENV NUM_THREADS 1000
ENV BPBOT TorchBot
ENV TORCHBOT_MODEL best_lstm2.pth

RUN INSTALL_TORCHBOT=1 python3 setup.py install

ENV BOT TorchBot
CMD [ "sh", "-c", "python3 -u webapp/server.py" ]
