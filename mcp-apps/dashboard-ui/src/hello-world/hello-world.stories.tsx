import type { Meta, StoryObj } from '@storybook/react';
import { HelloWorldWidget } from './hello-world';

const meta: Meta<typeof HelloWorldWidget> = {
  title: 'MCP UI/Hello World',
  component: HelloWorldWidget,
  parameters: {
    layout: 'centered',
  },
  args: {
    name: '',
  },
};

export default meta;
type Story = StoryObj<typeof HelloWorldWidget>;

export const Default: Story = {};

export const WithName: Story = {
  args: {
    name: 'Copilot',
  },
};
