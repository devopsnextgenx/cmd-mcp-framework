import type { Meta, StoryObj } from '@storybook/react';
import { GreetWidget } from './greet';

const meta: Meta<typeof GreetWidget> = {
  title: 'MCP UI/Greetings',
  component: GreetWidget,
  parameters: {
    layout: 'centered',
  },
  args: {
    name: '',
  },
};

export default meta;
type Story = StoryObj<typeof GreetWidget>;

export const Default: Story = {};

export const WithName: Story = {
  args: {
    name: 'Copilot',
  },
};
