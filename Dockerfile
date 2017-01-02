FROM rsmmr/clang

RUN \
	apt-get update \
	&& apt-get install -y \
		ccache \
		stow \
        time

CMD bash
