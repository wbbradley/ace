FROM debian:latest
MAINTAINER William Bradley <williambbradley@gmail.com>

RUN apt update -y && apt install -y \
  less \
  vim \
  man

ADD ./install-deps.sh /opt
RUN /opt/install-deps.sh

RUN echo "nmap ; :" >> /root/.vimrc
RUN echo "imap jk <Esc>" >> /root/.vimrc
RUN echo "imap kj <Esc>" >> /root/.vimrc

ADD . /opt/zion
WORKDIR /opt/zion

CMD bash
