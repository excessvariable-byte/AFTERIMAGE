# Neural Network Architectures: CNNs, LSTMs, and Transformers

Convolutional Neural Networks (CNNs) use learned spatial filters that slide across an input. Local receptive fields and weight sharing make CNNs effective for images and other grid-like signals. Pooling or strided convolution can reduce spatial resolution while increasing the receptive field.

Long Short-Term Memory networks (LSTMs) are recurrent neural networks designed to preserve useful information over long sequences. Input, forget, and output gates control the cell state. An LSTM processes sequence positions in order, which can limit parallel execution across time steps, but its recurrent state is useful for streaming and sequential tasks.

Transformer architectures replace recurrence with self-attention and feed-forward layers. Self-attention compares query, key, and value vectors so each token can combine information from other positions. Multiple attention heads learn different relationships, while residual connections and normalisation stabilise deep stacks. Because a sequence's token representations can be processed in parallel during training, Transformers scale effectively on modern accelerators.

The evolution from CNN spatial filters and LSTM recurrence to parallel Transformer self-attention reflects different assumptions about locality, sequence order, and compute. Positional encodings or position embeddings provide order information that attention alone does not contain.
