/*
 * @brief Native TTY UART sample
 *
 * Copyright (c) 2023 Marko Sagadin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include <stdio.h>
#include <string.h>

const struct device *uart0 = DEVICE_DT_GET(DT_NODELABEL(uart0));
const struct device *uart2 = DEVICE_DT_GET(DT_NODELABEL(uart2));

struct fifo_item_t {
	void *fifo_reserved;
	char c;
};

char __aligned(8) buf[512 * sizeof(struct fifo_item_t)];
struct k_mem_slab slab;
struct k_fifo in_fifo;
struct k_fifo out_fifo;

static void uart_callback(const struct device* dev, void* user_data)
{
	int ret;

	if (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			struct fifo_item_t *rdata;
			ret = k_mem_slab_alloc(&slab, (void**) &rdata,
						K_NO_WAIT);
			__ASSERT(ret == 0, "Out of memory!\n");

			uart_fifo_read(dev, &(rdata->c), sizeof(char));
			k_fifo_put(&in_fifo, rdata);
		}

		if (uart_irq_tx_ready(dev)) {
			if (k_fifo_is_empty(&out_fifo)) {
				/* It is required to disable TX IRQs when
				there's no data, otherwise Interrupts will
				continuously fire and lock up the system */
				uart_irq_tx_disable(dev);
			} else {
				struct fifo_item_t *tdata;
				tdata = k_fifo_get(&out_fifo, K_NO_WAIT);
				if (tdata) {
					ret = uart_fifo_fill(dev,
						&(tdata->c), sizeof(char));
					__ASSERT(ret == sizeof(char),
							"UART TX failed!\n");
					k_mem_slab_free(&slab,
							(void*) tdata);
				}
			}
		}
	}
}

void send_str(const struct device *uart, char *str)
{
	int msg_len = strlen(str);
	int ret;
	for (int i = 0; i < msg_len; i++) {
			struct fifo_item_t *tdata;
			ret = k_mem_slab_alloc(&slab, (void**) &tdata,
					K_NO_WAIT);
			__ASSERT(ret == 0, "Out of memory!\n");
			tdata->c = str[i];
			k_fifo_put(&out_fifo, tdata);
		}
	uart_irq_tx_enable(uart);
	printk("Device %s sent: \"%s\"\n", uart->name, str);
}

void recv_str(const struct device *uart, char *str)
{
	char *head = str;

	while (!k_fifo_is_empty(&in_fifo)) {
		struct fifo_item_t *e = k_fifo_get(&in_fifo, K_NO_WAIT);
		*head++ = e->c;
		k_mem_slab_free(&slab, (void*) e);
	}
	*head = '\0';

	printk("Device %s received: \"%s\"\n", uart->name, str);
}

int main(void)
{
	int ret;
	char send_buf[64];
	char recv_buf[64];
	int i = 10;
	
	ret = k_mem_slab_init(&slab, buf, sizeof(struct fifo_item_t), 512);
	__ASSERT(ret == 0, "Can't allocate memory for slab.");
	k_fifo_init(&in_fifo);
	k_fifo_init(&out_fifo);
	
	uart_irq_rx_disable(uart0);
	uart_irq_tx_disable(uart0);
	uart_irq_rx_disable(uart2);
	uart_irq_tx_disable(uart2);

	uart_irq_callback_user_data_set(uart0, uart_callback, NULL);
			
	uart_irq_callback_user_data_set(uart2, uart_callback, NULL);
	// Only activate TX when there's data, otherwise system will hang
	uart_irq_rx_enable(uart2);
	
	while (i--) {
		snprintf(send_buf, 64, "Hello from device %s, num %d", uart0->name, i);
		send_str(uart0, send_buf);
		/* Wait some time for the messages to arrive to the second uart. */
		k_sleep(K_MSEC(100));
		recv_str(uart2, recv_buf);

		k_sleep(K_MSEC(1000));
	}

	return 0;
}
